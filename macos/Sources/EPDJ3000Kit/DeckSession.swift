// One deck as the app sees it: the CoreMIDI transport, a DeviceClient for the SysEx protocol, a DeckModel that turns
// the unit's MIDI into player commands, and the DeckAudio that makes the sound. Also the "on-screen deck": the same
// thing with no unit behind it, so the player works without hardware.
//
// Threading: everything here is on the main actor. MIDI arrives on CoreMIDI's thread, is timestamped there, and is
// handed over through an AsyncStream; the main actor then parses it and drives the DeckModel. That puts main-thread
// scheduling jitter between a platter tick and the player (see `inputDelayMs`).
//
// Fast-changing values (`model.controller`, `telemetry`) are plain properties, not @Published: at 500 Hz jog they
// would invalidate every view on every message. Views read them on a timeline (`Live`) instead.

import Combine
import EPDJ3000Engine
import EPDJ3000KitPure
import EPDJ3000Protocol
import Foundation

@MainActor
public final class DeckSession: ObservableObject, Identifiable {
    public enum Phase: Equatable {
        case connecting
        case ready
        case failed(String)
    }

    public static let telemetryRateHz = 50

    public nonisolated let id: String
    public let isVirtual: Bool
    public let unitName: String
    public let deckHint: Deck?
    public let audio: DeckAudio
    public let client: DeviceClient?

    @Published public private(set) var phase: Phase
    @Published public private(set) var info: Info?
    @Published public private(set) var config: [ConfigKey: Int32] = [:]
    @Published public private(set) var peers: [Peer] = []
    @Published public private(set) var peersKnown = false
    /// Last command failure, for a dismissible banner.
    @Published public var notice: String?
    /// Light the unit's PLAY and CUE LEDs from the player's state.
    @Published public var ledFeedback = true {
        didSet { if ledFeedback { model.resetFeedback() } }
    }

    public private(set) var model: DeckModel
    public private(set) var telemetry: Telemetry?
    /// Telemetry frames per second over the last second.
    public private(set) var telemetryHz = 0.0
    /// Time from a MIDI packet's arrival (CoreMIDI timestamp) to the main actor handling it, over the last second.
    public private(set) var inputDelayMs: (average: Double, max: Double) = (0, 0)

    private let log: MidiLog
    private let transport: CoreMidiTransport?
    private var auxInputs: [CoreMidiInput] = []
    private var parser = MidiStreamParser()
    private var tasks: [Task<Void, Never>] = []

    private var telemetryUsers = 0
    private var streamActive = false
    private var reconciling = false

    private var statsStart = HostClock.now()
    private var telemetryFrames = 0
    private var delaySum = 0.0
    private var delayMax = 0.0
    private var delayCount = 0

    // MARK: creation

    /// The on-screen deck: a player with no unit.
    public init(virtualWith log: MidiLog) {
        let audio = DeckAudio()
        self.id = "virtual"
        self.isVirtual = true
        self.unitName = "On-screen deck"
        self.deckHint = .a
        self.audio = audio
        self.client = nil
        self.transport = nil
        self.log = log
        self.phase = .ready
        self.model = DeckModel(player: audio.player, channel: Proto.defaultChannelDeckA)
        log.setName("On-screen deck", forSource: id)
    }

    /// Opens the unit's deck cable for SysEx and MIDI, and listens to its other cables for the monitor.
    /// Call `start()` once the session is stored somewhere.
    public init(unit: UnitEndpoints, service: CoreMidiService, log: MidiLog) throws {
        guard let deckCable = unit.cable(Int(Proto.Cable.deck)) else { throw CoreMidiError.noDestination }
        let transport = try service.openTransport(deckCable)
        let audio = DeckAudio()
        let id = "unit-\(unit.deviceUniqueID)"
        self.id = id
        self.isVirtual = false
        self.unitName = unit.deviceName
        self.deckHint = unit.deckHint
        self.audio = audio
        self.client = DeviceClient(transport: transport)
        self.transport = transport
        self.log = log
        self.phase = .connecting
        let hintedDeck = unit.deckHint
        self.model = DeckModel(player: audio.player, channel: hintedDeck == .b ? Proto.defaultChannelDeckB : Proto.defaultChannelDeckA)
        log.setName(hintedDeck.map { "Deck \($0 == .a ? "A" : "B")?" } ?? unit.deviceName, forSource: id)

        for cable in unit.cables where cable.index != Int(Proto.Cable.deck) {
            let monitor = AuxMonitor(log: log, source: id, cable: cable.label)
            if let input = try? service.openInput(cable, handler: { monitor.handle($0) }) {
                auxInputs.append(input)
            }
        }
    }

    public func start() {
        guard let client, let transport else { return }
        transport.setTxObserver { [log, id] time, bytes in
            if let first = bytes.first, first == 0xF0, MidiFormat.isTelemetryFrame(bytes) { return }
            log.append(source: id, direction: .tx, bytes: bytes, time: time)
        }
        let timed = transport.timedInput
        spawn { [weak self] in
            for await chunk in timed {
                guard let self else { return }
                self.ingest(chunk)
            }
        }
        let frames = client.telemetry()
        spawn { [weak self] in
            for await frame in frames {
                guard let self else { return }
                self.telemetry = frame
                self.telemetryFrames += 1
            }
        }
        startTicking()
        spawn { [weak self] in await self?.handshake(client) }
    }

    /// A task that `shutdown()` cancels.
    private func spawn(_ body: @escaping @MainActor () async -> Void) {
        tasks.append(Task { await body() })
    }

    /// Stops everything and releases the unit. The session is unusable afterwards.
    public func shutdown() {
        for task in tasks { task.cancel() }
        tasks.removeAll()
        transport?.setTxObserver(nil)
        for input in auxInputs { input.close() }
        auxInputs.removeAll()
        transport?.close()
        if let client { Task { await client.close() } }
        audio.shutdown()
    }

    // MARK: identity

    /// Confirmed by GET_INFO once `info` is set; until then the name's hint, else A.
    public var deck: Deck {
        if let info, let confirmed = Deck(rawValue: info.deck) { return confirmed }
        return deckHint ?? .a
    }

    public var isDeckConfirmed: Bool { info != nil }

    public var label: String {
        isVirtual ? "On-screen deck" : "Deck \(deck == .a ? "A" : "B")" + (isDeckConfirmed ? "" : "?")
    }

    public var caps: Caps { Caps(rawValue: info?.caps ?? 0) }

    public var isReady: Bool { phase == .ready }

    /// What the Rig checklist judges.
    public var rigUnit: RigUnit {
        RigUnit(name: unitName, deck: isDeckConfirmed ? deck : nil, caps: info.map { Caps(rawValue: $0.caps) },
                flags: telemetry.map { StateFlags(rawValue: $0.flags) }, peers: peersKnown ? peers : nil)
    }

    // MARK: MIDI in

    private func ingest(_ chunk: TimedMidi) {
        let delay = max(0, HostClock.now() - chunk.time)
        delaySum += delay
        delayMax = max(delayMax, delay)
        delayCount += 1
        for event in parser.feed(chunk.bytes) {
            if case let .sysex(bytes) = event, MidiFormat.isTelemetryFrame(bytes) { continue }
            log.append(source: id, direction: .rx, event: event, time: chunk.time)
            model.handle(event, at: chunk.time)
        }
    }

    /// Feeds an event to the deck model as if the unit had sent it (the on-screen platter).
    public func inject(_ event: MidiEvent) {
        let now = HostClock.now()
        log.append(source: id, direction: .rx, event: event, time: now)
        model.handle(event, at: now)
    }

    public func virtualTouch(_ down: Bool) {
        let channel = model.controller.channel
        inject(down ? .noteOn(channel: channel, note: Proto.Note.touch, velocity: 127)
                    : .noteOff(channel: channel, note: Proto.Note.touch, velocity: 0))
    }

    /// Splits into messages of at most 63 ticks, like the unit does.
    public func virtualJog(ticks: Int) {
        var remaining = ticks
        while remaining != 0 {
            let step = min(63, max(-63, remaining))
            inject(.jog(delta: step, channel: model.controller.channel))
            remaining -= step
        }
    }

    // MARK: ticking

    private func startTicking() {
        spawn { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 33_000_000)
                guard let self else { return }
                self.tick()
            }
        }
    }

    private func tick() {
        let now = HostClock.now()
        if now - statsStart >= 1 {
            telemetryHz = Double(telemetryFrames) / (now - statsStart)
            inputDelayMs = delayCount > 0 ? (delaySum / Double(delayCount) * 1000, delayMax * 1000) : (0, 0)
            telemetryFrames = 0
            delaySum = 0
            delayMax = 0
            delayCount = 0
            statsStart = now
        }
        if ledFeedback, let client, phase == .ready {
            let events = model.feedback()
            if !events.isEmpty {
                Task { try? await client.send(events) }
            }
        }
    }

    // MARK: handshake

    private enum HandshakeError: LocalizedError {
        case badDeck(UInt8)

        var errorDescription: String? {
            switch self {
            case let .badDeck(id): return "The unit reports deck id \(id), which is not A or B."
            }
        }
    }

    private func handshake(_ client: DeviceClient) async {
        phase = .connecting
        do {
            var found: Info?
            var failure: Error?
            // The firmware may still be starting right after USB enumeration.
            for _ in 0..<3 {
                do {
                    found = try await client.getInfo()
                    break
                } catch is CancellationError {
                    return
                } catch {
                    failure = error
                }
            }
            guard let info = found else { throw failure ?? DeviceError.timeout(.getInfo) }
            guard info.deck < 16 else { throw HandshakeError.badDeck(info.deck) }
            await client.setTarget(info.deck)
            self.info = info
            config = await readAllConfig(client)
            rebuildModelIfNeeded()
            model.resetFeedback()
            log.setName(label, forSource: id)
            phase = .ready
            reconcileStream()
        } catch is CancellationError {
            return
        } catch {
            phase = .failed(ErrorText.describe(error))
        }
    }

    /// Tries again after a failed handshake.
    public func retry() {
        guard let client, phase != .connecting else { return }
        spawn { [weak self] in await self?.handshake(client) }
    }

    private func readAllConfig(_ client: DeviceClient) async -> [ConfigKey: Int32] {
        if let all = try? await client.dumpConfig() { return all }
        // The dump ends with an ACK, so a failure here is a unit that never sent it (older firmware) or a lost frame; ask key by key.
        var out = [ConfigKey: Int32]()
        var failures = 0
        for key in ConfigKey.allCases where failures < 3 {
            if let value = try? await client.getConfig(key) { out[key] = value } else { failures += 1 }
        }
        return out
    }

    // MARK: channel

    private func desiredChannel() -> UInt8 {
        if let c = config[.midiChannel], (0...15).contains(c) { return UInt8(c) }
        return deck == .b ? Proto.defaultChannelDeckB : Proto.defaultChannelDeckA
    }

    private func rebuildModelIfNeeded() {
        let channel = desiredChannel()
        guard channel != model.controller.channel else { return }
        let old = model
        model = DeckModel(player: audio.player, channel: channel)
        model.cutCurve = old.cutCurve
        model.pitchRange = old.pitchRange
        model.trackBpm = old.trackBpm
    }

    // MARK: commands

    @discardableResult
    public func setConfig(_ key: ConfigKey, _ value: Int32) async -> Bool {
        guard let client else { return false }
        do {
            try await client.setConfig(key, value)
            config[key] = value
            if key == .midiChannel { rebuildModelIfNeeded() }
            return true
        } catch {
            notice = "\(ConfigTable.desc(key).name): \(ErrorText.describe(error))"
            return false
        }
    }

    @discardableResult
    public func apply(_ updates: [ConfigUpdate]) async -> Bool {
        for update in updates {
            guard await setConfig(update.key, update.value) else { return false }
        }
        return true
    }

    @discardableResult
    public func saveConfig() async -> Bool {
        await run("Save") { try await $0.saveConfig() }
    }

    public func loadDefaults() async {
        guard await run("Load defaults", { try await $0.loadDefaults() }) else { return }
        await reloadConfig()
    }

    public func reloadConfig() async {
        guard let client else { return }
        config = await readAllConfig(client)
        rebuildModelIfNeeded()
    }

    public func setDeck(_ new: Deck) async {
        if await run("Swap deck", { try await $0.setDeck(new) }) {
            notice = "The unit is restarting as Deck \(new == .a ? "A" : "B"); it will reappear in a moment."
        }
    }

    public func reboot(bootloader: Bool) async {
        if await run("Reboot", { try await $0.reboot(bootloader ? .bootloader : .normal) }) {
            notice = bootloader ? "The unit is in its ROM bootloader and will not answer until it is flashed or power-cycled."
                                : "The unit is restarting; it will reappear in a moment."
        }
    }

    public func identify(seconds: UInt8 = 3) async {
        await run("Identify") { try await $0.identify(seconds: seconds) }
    }

    public func deviceTone(on: Bool, frequency: UInt16 = 440, attenuationDb: UInt8 = 12) async {
        await run("Test tone") { try await $0.testTone(on: on, freqHz: frequency, attenDb: attenuationDb) }
    }

    public func refreshLink() async {
        guard let client, phase == .ready else { return }
        if let found = try? await client.linkStatus() {
            peers = found
            peersKnown = true
        }
    }

    /// Transport and tempo to the unit (the `transport` and `bpm` controls are "both" in the contract).
    public func sendTransport(_ state: TransportState) {
        send([.controlChange(channel: model.controller.channel, controller: Proto.CC.transport, value: state.rawValue)])
    }

    public func sendBpm(x10: Int) {
        let clamped = min(Proto.bpmMaxX10, max(Proto.bpmMinX10, x10))
        send(MidiEvent.cc14(.bpm, channel: model.controller.channel, value: UInt16(clamped)))
    }

    public func nudgeBpm(steps: Int) {
        send([.controlChange(channel: model.controller.channel, controller: Proto.CC.bpmNudge,
                             value: MidiEncoding.encodeRelative(steps))])
    }

    private func send(_ events: [MidiEvent]) {
        guard let client else { return }
        Task {
            do { try await client.send(events) } catch { notice = ErrorText.describe(error) }
        }
    }

    @discardableResult
    private func run(_ what: String, _ body: (DeviceClient) async throws -> Void) async -> Bool {
        guard let client else { return false }
        do {
            try await body(client)
            return true
        } catch {
            notice = "\(what): \(ErrorText.describe(error))"
            return false
        }
    }

    // MARK: telemetry stream

    /// Starts the unit's telemetry stream (reference counted: each caller balances with `releaseTelemetry`).
    public func acquireTelemetry() {
        telemetryUsers += 1
        reconcileStream()
    }

    public func releaseTelemetry() {
        telemetryUsers = max(0, telemetryUsers - 1)
        reconcileStream()
    }

    private func reconcileStream() {
        guard let client, phase == .ready, !reconciling else { return }
        reconciling = true
        Task { [weak self] in await self?.runReconcile(client) }
    }

    private func runReconcile(_ client: DeviceClient) async {
        defer { reconciling = false }
        while true {
            let want = telemetryUsers > 0
            if want == streamActive { return }
            do {
                if want {
                    try await client.startStream(rateHz: Self.telemetryRateHz)
                } else {
                    try await client.stopStream()
                    telemetry = nil
                }
                streamActive = want
            } catch {
                notice = "Telemetry stream: \(ErrorText.describe(error))"
                return
            }
        }
    }
}

/// Parses and logs a cable nobody is talking SysEx on (TRS jack, linked deck).
private final class AuxMonitor: @unchecked Sendable {
    private var parser = MidiStreamParser()
    private let log: MidiLog
    private let source: String
    private let cable: String

    init(log: MidiLog, source: String, cable: String) {
        self.log = log
        self.source = source
        self.cable = cable
    }

    func handle(_ chunk: TimedMidi) {
        for event in parser.feed(chunk.bytes) {
            log.append(source: source, cable: cable, direction: .rx, event: event, time: chunk.time)
        }
    }
}

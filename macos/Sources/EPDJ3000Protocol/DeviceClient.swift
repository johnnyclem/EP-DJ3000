// Request/response client for the SysEx configuration protocol over any byte transport.
// Pure Swift: CoreMIDI (or a fake) plugs in behind `MidiTransport`.

import Foundation

/// One bidirectional MIDI port to a single unit (the "Deck" cable).
public protocol MidiTransport: AnyObject, Sendable {
    /// Sends raw MIDI bytes: one or more complete messages, a SysEx always whole (`F0 ... F7`).
    func send(_ bytes: [UInt8]) async throws
    /// Bytes arriving from the device, in any chunking, real-time bytes possibly interleaved.
    /// Single consumer: `DeviceClient` takes this once. It finishes when the port goes away.
    var incoming: AsyncStream<[UInt8]> { get }
}

/// Source of timeouts, injectable so tests do not wait in real time.
public protocol DeviceClock: Sendable {
    /// Must throw `CancellationError` (as `Task.sleep` does) when the calling task is cancelled.
    func sleep(seconds: Double) async throws
}

public struct SystemClock: DeviceClock {
    public init() {}
    public func sleep(seconds: Double) async throws {
        try await Task.sleep(nanoseconds: UInt64(max(0, seconds) * 1_000_000_000))
    }
}

public enum DeviceError: Error, Equatable, Sendable {
    case timeout(SysExCmd)
    case nak(SysExCmd, SysExError)
    /// NAK with an error code this build does not know.
    case unknownNak(SysExCmd, code: UInt8)
    /// The reply arrived but its payload could not be parsed.
    case malformedReply(SysExCmd)
    /// The unit speaks another protocol version; nothing else can be assumed about it.
    case protocolMismatch(Info)
    case transportClosed
    case sendFailed(String)
}

/// Requests are strictly serialised and answered by reply code (the protocol has no request ids), so a reply that
/// arrives after its request timed out can be taken for the answer to the next request of the same command.
/// A cancelled caller gets `CancellationError`; every other failure is a `DeviceError`.
public actor DeviceClient {
    public static let maxStreamRateHz = 100

    private let transport: MidiTransport
    private let clock: DeviceClock
    private let timeout: Double
    private let slowTimeout: Double
    private let streamRefreshInterval: Double

    /// Deck id requests are addressed to and replies are accepted from; `Proto.SysEx.deckAny` (127) = any.
    public private(set) var target: UInt8
    /// Frames dropped for a bad checksum/version/packing or an unparsable telemetry payload (bring-up aid).
    public private(set) var rejectedFrameCount = 0
    public private(set) var isStreaming = false

    private let telemetryFanout = Fanout<Telemetry>(policy: .bufferingNewest(256))
    // Unbounded: jog is relative, so dropping an event would permanently shift the platter position.
    private let eventFanout = Fanout<MidiEvent>(policy: .unbounded)
    private let receiveSlot = TaskSlot()
    private let keepAliveSlot = TaskSlot()
    private var parser = MidiStreamParser()
    private var streamRate: UInt8 = 0
    private var closed = false

    private var busy = false
    private var waiters: [CheckedContinuation<Void, Never>] = []
    private var pending: Pending?
    private var nextRequestId = 0

    /// - Parameters:
    ///   - timeout: how long a request waits for its reply.
    ///   - slowTimeout: same for commands that touch flash (SAVE_CONFIG, SET_DECK).
    ///   - streamRefreshInterval: the device stops streaming after 5 s without a refresh, so the client re-sends STREAM this often.
    public init(transport: MidiTransport,
                deck: UInt8 = Proto.SysEx.deckAny,
                clock: DeviceClock = SystemClock(),
                timeout: Double = 1.0,
                slowTimeout: Double = 3.0,
                streamRefreshInterval: Double = 2.0) {
        precondition(Self.isValidTarget(deck), "deck must be 0...15 or Proto.SysEx.deckAny")
        self.transport = transport
        self.target = deck
        self.clock = clock
        self.timeout = timeout
        self.slowTimeout = slowTimeout
        self.streamRefreshInterval = streamRefreshInterval
        let incoming = transport.incoming
        // Weak: a client that is dropped without close() must not be kept alive by its own receive loop.
        receiveSlot.set(Task { [weak self] in
            for await chunk in incoming {
                guard let self else { return }
                await self.ingest(chunk)
            }
            await self?.transportEnded()
        })
    }

    deinit {
        receiveSlot.cancel()
        keepAliveSlot.cancel()
        telemetryFanout.finish()
        eventFanout.finish()
    }

    // MARK: streams

    /// Telemetry frames while streaming. Subscribe before `startStream`: nothing is replayed.
    public nonisolated func telemetry() -> AsyncStream<Telemetry> {
        telemetryFanout.makeStream()
    }

    /// Everything the unit sends that is not an EP-DJ3000 SysEx frame: notes, CCs, clock, MTC, foreign SysEx.
    /// Lossless and unbounded, so a subscriber must keep draining (or drop the stream).
    public nonisolated func events() -> AsyncStream<MidiEvent> {
        eventFanout.makeStream()
    }

    // MARK: lifecycle

    public func setTarget(_ deck: UInt8) {
        precondition(Self.isValidTarget(deck), "deck must be 0...15 or Proto.SysEx.deckAny")
        target = deck
    }

    private static func isValidTarget(_ deck: UInt8) -> Bool {
        deck < 16 || deck == Proto.SysEx.deckAny
    }

    /// Stops background work, fails any in-flight request and ends all streams. Does not close the transport.
    public func close() {
        guard !closed else { return }
        closed = true
        receiveSlot.cancel()
        keepAliveSlot.cancel()
        isStreaming = false
        fail(DeviceError.transportClosed)
        telemetryFanout.finish()
        eventFanout.finish()
    }

    // MARK: commands

    public func getInfo() async throws -> Info {
        let frames = try await perform(Exchange(command: .getInfo, reply: .info, timeout: timeout))
        guard let info = Info(bytes: frames[0].payload) else { throw DeviceError.malformedReply(.getInfo) }
        guard info.protocolVersion == Proto.protocolVersion else { throw DeviceError.protocolMismatch(info) }
        return info
    }

    public func getConfig(_ key: ConfigKey) async throws -> Int32 {
        let frames = try await perform(Exchange(
            command: .getConfig, payload: [key.rawValue], reply: .config, timeout: timeout,
            accepts: { frame, _ in frame.payload.first == key.rawValue }))
        guard let (_, value) = Self.parseConfig(frames[0]) else { throw DeviceError.malformedReply(.getConfig) }
        return value
    }

    /// Every key this build knows. Completes when all of them arrived; a unit that stops short times out.
    public func dumpConfig() async throws -> [ConfigKey: Int32] {
        let frames = try await perform(Exchange(
            command: .getConfig, payload: [0xFF], reply: .config, expectedFrames: ConfigKey.allCases.count, timeout: timeout,
            accepts: { frame, seen in
                guard let (key, _) = Self.parseConfig(frame) else { return false }
                return !seen.contains { Self.parseConfig($0)?.0 == key }
            }))
        var out = [ConfigKey: Int32]()
        for f in frames {
            guard let (key, value) = Self.parseConfig(f) else { throw DeviceError.malformedReply(.getConfig) }
            out[key] = value
        }
        return out
    }

    public func setConfig(_ key: ConfigKey, _ value: Int32) async throws {
        var payload: [UInt8] = [key.rawValue]
        Self.appendInt32(value, to: &payload)
        _ = try await perform(Exchange(command: .setConfig, payload: payload, timeout: timeout))
    }

    /// Applies updates in order; stops at the first failure.
    public func apply(_ updates: [ConfigUpdate]) async throws {
        for u in updates { try await setConfig(u.key, u.value) }
    }

    public func saveConfig() async throws {
        _ = try await perform(Exchange(command: .saveConfig, timeout: slowTimeout))
    }

    public func loadDefaults() async throws {
        _ = try await perform(Exchange(command: .loadDefaults, timeout: timeout))
    }

    /// Starts telemetry at `rateHz` (clamped to 1...100) and keeps it alive until `stopStream`.
    public func startStream(rateHz: Int) async throws {
        let rate = UInt8(min(Self.maxStreamRateHz, max(1, rateHz)))
        _ = try await perform(Exchange(command: .stream, payload: [rate], timeout: timeout))
        streamRate = rate
        isStreaming = true
        keepAliveSlot.set(Task { [weak self, clock, interval = streamRefreshInterval] in
            var failures = 0
            while !Task.isCancelled {
                do { try await clock.sleep(seconds: interval) } catch { return }
                guard let self else { return }
                failures = await self.refreshStream() ? 0 : failures + 1
                if failures >= 3 {
                    await self.streamLost()
                    return
                }
            }
        })
    }

    public func stopStream() async throws {
        keepAliveSlot.cancel()
        streamRate = 0
        isStreaming = false
        _ = try await perform(Exchange(command: .stream, payload: [0], timeout: timeout))
    }

    /// CAL_BEGIN: the device starts recording the extremes of the control.
    public func calAutoBegin(_ kind: CalKind) async throws {
        _ = try await perform(Exchange(command: .calBegin, payload: [kind.rawValue], timeout: timeout))
    }

    /// CAL_END: with `apply` the device writes the measured range into its live config.
    public func calAutoEnd(_ kind: CalKind, apply: Bool) async throws -> CalResult {
        let frames = try await perform(Exchange(
            command: .calEnd, payload: [kind.rawValue, apply ? 1 : 0], reply: .calResult, timeout: timeout))
        guard let result = CalResult(bytes: frames[0].payload) else { throw DeviceError.malformedReply(.calEnd) }
        return result
    }

    /// The unit re-enumerates afterwards, so the transport is expected to go away; the client then addresses the new id.
    public func setDeck(_ deck: Deck) async throws {
        _ = try await perform(Exchange(command: .setDeck, payload: [deck.rawValue], timeout: slowTimeout))
        if target != Proto.SysEx.deckAny { target = deck.rawValue }
    }

    public func reboot(_ mode: RebootMode = .normal) async throws {
        _ = try await perform(Exchange(command: .reboot, payload: [mode.rawValue], timeout: timeout))
    }

    public func linkStatus() async throws -> [Peer] {
        let frames = try await perform(Exchange(command: .linkStatus, reply: .link, timeout: timeout))
        let p = frames[0].payload
        guard let count = p.first.map(Int.init), p.count >= 1 + count * Peer.byteCount else {
            throw DeviceError.malformedReply(.linkStatus)
        }
        return (0..<count).compactMap { i in
            let start = 1 + i * Peer.byteCount
            return Peer(bytes: Array(p[start..<(start + Peer.byteCount)]))
        }
    }

    public func testTone(on: Bool, freqHz: UInt16 = 440, attenDb: UInt8 = 12) async throws {
        let payload: [UInt8] = [on ? 1 : 0, UInt8(freqHz & 0xFF), UInt8(freqHz >> 8), attenDb]
        _ = try await perform(Exchange(command: .testTone, payload: payload, timeout: timeout))
    }

    public func identify(seconds: UInt8 = 3) async throws {
        _ = try await perform(Exchange(command: .identify, payload: [seconds], timeout: timeout))
    }

    /// Sends MIDI to the unit (transport CC, BPM, LED feedback notes). Not a request: no reply is awaited.
    public func send(_ events: [MidiEvent]) async throws {
        guard !closed else { throw DeviceError.transportClosed }
        do { try await transport.send(events.flatMap(\.bytes)) } catch { throw DeviceError.sendFailed(String(describing: error)) }
    }

    // MARK: request machinery

    private struct Exchange {
        var command: SysExCmd
        var payload: [UInt8] = []
        /// Data reply that completes the request; nil = a plain ACK does.
        var reply: SysExReply?
        var expectedFrames = 1
        var timeout: Double
        /// Whether a reply frame belongs to this request, given the frames already collected.
        var accepts: (SysExFrame, [SysExFrame]) -> Bool = { _, _ in true }
    }

    private struct Pending {
        var id: Int
        var exchange: Exchange
        var frames: [SysExFrame] = []
        var continuation: CheckedContinuation<[SysExFrame], Error>
        var timeoutTask: Task<Void, Never>?
    }

    private func perform(_ x: Exchange) async throws -> [SysExFrame] {
        await acquire()
        defer { release() }
        guard !closed else { throw DeviceError.transportClosed }
        try Task.checkCancellation()

        let id = nextRequestId
        nextRequestId += 1
        let bytes = SysExFrame(deck: target, command: x.command, payload: x.payload).encode()
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<[SysExFrame], Error>) in
                // Registered before sending: a fast device may answer inside `send`.
                pending = Pending(id: id, exchange: x, continuation: continuation)
                pending?.timeoutTask = Task { [clock] in
                    do { try await clock.sleep(seconds: x.timeout) } catch { return }
                    self.expire(id)
                }
                Task {
                    do { try await self.transport.send(bytes) } catch {
                        self.complete(id, .failure(DeviceError.sendFailed(String(describing: error))))
                    }
                }
            }
        } onCancel: {
            Task { await self.cancelRequest(id) }
        }
    }

    /// Requests run strictly one at a time: the protocol correlates replies by code, not by id.
    private func acquire() async {
        if !busy {
            busy = true
            return
        }
        await withCheckedContinuation { waiters.append($0) }
    }

    private func release() {
        if waiters.isEmpty { busy = false } else { waiters.removeFirst().resume() }  // hand over, stay busy
    }

    private func complete(_ id: Int, _ result: Result<[SysExFrame], Error>) {
        guard let p = pending, p.id == id else { return }
        pending = nil
        p.timeoutTask?.cancel()
        p.continuation.resume(with: result)
    }

    private func fail(_ error: Error) {
        if let p = pending { complete(p.id, .failure(error)) }
    }

    private func expire(_ id: Int) {
        guard let p = pending, p.id == id else { return }
        complete(id, .failure(DeviceError.timeout(p.exchange.command)))
    }

    private func cancelRequest(_ id: Int) {
        complete(id, .failure(CancellationError()))
    }

    private func refreshStream() async -> Bool {
        guard streamRate > 0, !closed, !Task.isCancelled else { return true }  // stopped meanwhile: not a failure
        do {
            _ = try await perform(Exchange(command: .stream, payload: [streamRate], timeout: timeout))
            return true
        } catch {
            return false
        }
    }

    private func streamLost() {
        streamRate = 0
        isStreaming = false
    }

    // MARK: receiving

    private func ingest(_ chunk: [UInt8]) {
        for event in parser.feed(chunk) {
            guard case let .sysex(bytes) = event else {
                eventFanout.yield(event)
                continue
            }
            do {
                route(try SysExFrame.decode(bytes))
            } catch SysExFrameError.notEPDJ3000 {
                eventFanout.yield(event)
            } catch {
                rejectedFrameCount += 1
            }
        }
    }

    private func route(_ frame: SysExFrame) {
        // PROTOCOL.md does not say whether SET_DECK's ACK carries the old or the new id; accept both, since
        // the unit reboots right after and a timeout would misreport a command that worked.
        let isSetDeckAck = pending.map { $0.exchange.command == .setDeck && frame.deck == $0.exchange.payload.first } ?? false
        guard target == Proto.SysEx.deckAny || frame.deck == target || isSetDeckAck else { return }
        guard let p = pending else {
            routeUnsolicited(frame)
            return
        }
        switch frame.kind {
        case let .ack(command) where command == p.exchange.command.rawValue && p.exchange.reply == nil:
            complete(p.id, .success([frame]))
        case let .nak(command, code) where command == p.exchange.command.rawValue:
            if let e = SysExError(rawValue: code) {
                complete(p.id, .failure(DeviceError.nak(p.exchange.command, e)))
            } else {
                complete(p.id, .failure(DeviceError.unknownNak(p.exchange.command, code: code)))
            }
        case let .reply(reply) where reply == p.exchange.reply && p.exchange.accepts(frame, p.frames):
            pending?.frames.append(frame)
            if p.frames.count + 1 >= p.exchange.expectedFrames { complete(p.id, .success(p.frames + [frame])) }
        default:
            routeUnsolicited(frame)
        }
    }

    private func routeUnsolicited(_ frame: SysExFrame) {
        guard case .reply(.telemetry) = frame.kind else { return }  // stale replies and foreign ACKs are dropped
        if let t = Telemetry(bytes: frame.payload) { telemetryFanout.yield(t) } else { rejectedFrameCount += 1 }
    }

    private func transportEnded() {
        guard !closed else { return }
        close()
    }

    // MARK: payload helpers

    private static func appendInt32(_ v: Int32, to out: inout [UInt8]) {
        let u = UInt32(bitPattern: v)
        for i in 0..<4 { out.append(UInt8((u >> UInt32(8 * i)) & 0xFF)) }
    }

    private static func parseConfig(_ frame: SysExFrame) -> (ConfigKey, Int32)? {
        let p = frame.payload
        guard p.count >= 5, let key = ConfigKey(rawValue: p[0]) else { return nil }
        var u: UInt32 = 0
        for i in 0..<4 { u |= UInt32(p[1 + i]) << UInt32(8 * i) }
        return (key, Int32(bitPattern: u))
    }
}

/// Holds one background task so it can be replaced or cancelled from `init`/`deinit`, where an actor's own state is off limits.
final class TaskSlot: @unchecked Sendable {
    private let lock = NSLock()
    private var task: Task<Void, Never>?

    func set(_ new: Task<Void, Never>?) {
        lock.lock()
        let old = task
        task = new
        lock.unlock()
        old?.cancel()
    }

    func cancel() { set(nil) }
}

/// Broadcasts values to any number of `AsyncStream` subscribers.
final class Fanout<Element: Sendable>: @unchecked Sendable {
    private let policy: AsyncStream<Element>.Continuation.BufferingPolicy
    private let lock = NSLock()
    private var continuations: [Int: AsyncStream<Element>.Continuation] = [:]
    private var nextId = 0
    private var finished = false

    init(policy: AsyncStream<Element>.Continuation.BufferingPolicy) {
        self.policy = policy
    }

    func makeStream() -> AsyncStream<Element> {
        AsyncStream(bufferingPolicy: policy) { continuation in
            lock.lock()
            defer { lock.unlock() }
            guard !finished else {
                continuation.finish()
                return
            }
            let id = nextId
            nextId += 1
            continuations[id] = continuation
            continuation.onTermination = { [weak self] _ in self?.remove(id) }
        }
    }

    func yield(_ value: Element) {
        lock.lock()
        let targets = Array(continuations.values)
        lock.unlock()
        for c in targets { c.yield(value) }
    }

    func finish() {
        lock.lock()
        finished = true
        let targets = Array(continuations.values)
        continuations.removeAll()
        lock.unlock()
        for c in targets { c.finish() }
    }

    private func remove(_ id: Int) {
        lock.lock()
        continuations[id] = nil
        lock.unlock()
    }
}

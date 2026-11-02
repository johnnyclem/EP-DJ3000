import Foundation
@testable import EPDJ3000Protocol

/// In-memory EP-DJ3000, written against docs/PROTOCOL.md section 3 (not against DeviceClient).
/// Answers a request only if its deck byte matches the unit's deck id or is 127.
final class FakeDevice: MidiTransport, @unchecked Sendable {
    struct State {
        var deck: UInt8
        var config = ConfigTable.all.map(\.defaultValue)
        /// Keys this "firmware" knows; lower it to simulate an older unit (BAD_KEY above it).
        var keyCount = ConfigKey.allCases.count
        var info = Info(protocolVersion: Proto.protocolVersion, fwMajor: 0, fwMinor: 1, fwPatch: 2, hwRev: 1, deck: 0, mode: 0,
                        platterSource: 1, caps: 0x3E, serial: 0xDEADBEEF, uptimeS: 3600)
        var peers: [Peer] = []
        var hasDac = true
        var failFlash = false

        // Behaviour switches
        var silent = false
        var holdReplies = false
        var chunkSize = 0
        var interleaveRealtime = false
        var forcedNak: (command: UInt8, code: UInt8)?
        var setDeckAcksWithNewId = false
        var sendError: Error?

        // Observations
        var requests: [SysExFrame] = []          // every valid frame received, whatever its deck
        var requestBytes: [[UInt8]] = []         // the raw bytes of those frames
        var midiIn: [MidiEvent] = []             // non-SysEx events received
        var streamRate: UInt8 = 0
        var streamRefreshes = 0
        var saveCount = 0
        var lastReboot: UInt8?
        var lastTone: [UInt8]?
        var lastIdentify: UInt8?
        var held: [[UInt8]] = []

        // Calibration (the device's min/max recorder)
        var calKind: CalKind?
        var calMin = Int.max
        var calMax = Int.min
    }

    let incoming: AsyncStream<[UInt8]>
    private let output: AsyncStream<[UInt8]>.Continuation
    private let lock = NSLock()
    private var state: State
    private var parser = MidiStreamParser()

    init(deck: Deck = .a) {
        var s = State(deck: deck.rawValue)
        s.info.deck = deck.rawValue
        state = s
        // AsyncStream.makeStream is macOS 14+; the package targets macOS 13.
        var continuation: AsyncStream<[UInt8]>.Continuation!
        incoming = AsyncStream { continuation = $0 }
        output = continuation
    }

    // MARK: test access

    func read<T>(_ body: (State) -> T) -> T {
        lock.lock()
        defer { lock.unlock() }
        return body(state)
    }

    func mutate(_ body: (inout State) -> Void) {
        lock.lock()
        body(&state)
        lock.unlock()
    }

    /// Delivers bytes to the host exactly as given (garbage, foreign SysEx, notes...).
    func inject(_ bytes: [UInt8]) {
        output.yield(bytes)
    }

    func inject(_ frame: SysExFrame) {
        output.yield(frame.encode())
    }

    func releaseHeld() {
        let held: [[UInt8]] = read { $0.held }
        mutate { $0.held.removeAll() }
        for h in held { output.yield(h) }
    }

    func disconnect() {
        output.finish()
    }

    /// The firmware's ADC loop: while a calibration is active it keeps the extremes.
    func moveControl(_ kind: CalKind, raw: Int) {
        mutate {
            guard $0.calKind == kind else { return }
            $0.calMin = min($0.calMin, raw)
            $0.calMax = max($0.calMax, raw)
        }
    }

    /// Emits a TELEMETRY frame if the host started a stream.
    func emit(_ t: Telemetry) {
        let (rate, deck): (UInt8, UInt8) = read { ($0.streamRate, $0.deck) }
        guard rate > 0 else { return }
        deliver(SysExFrame(deck: deck, reply: .telemetry, payload: t.serialized()).encode())
    }

    // MARK: MidiTransport

    func send(_ bytes: [UInt8]) async throws {
        if let e: Error = read({ $0.sendError }) { throw e }
        for r in process(bytes) { deliver(r.encode()) }
    }

    private func process(_ bytes: [UInt8]) -> [SysExFrame] {
        lock.lock()
        defer { lock.unlock() }
        var replies = [SysExFrame]()
        for event in parser.feed(bytes) {
            guard case let .sysex(raw) = event else {
                state.midiIn.append(event)
                continue
            }
            guard let frame = try? SysExFrame.decode(raw) else { continue }
            state.requests.append(frame)
            state.requestBytes.append(raw)
            guard !state.silent, frame.deck == state.deck || frame.deck == Proto.SysEx.deckAny else { continue }
            replies += handle(frame)
        }
        return replies
    }

    // MARK: delivery

    private func deliver(_ bytes: [UInt8]) {
        let (hold, chunk, interleave): (Bool, Int, Bool) = read { ($0.holdReplies, $0.chunkSize, $0.interleaveRealtime) }
        if hold {
            mutate { $0.held.append(bytes) }
            return
        }
        var wire = bytes
        if interleave {  // a real-time byte may sit between any two bytes, even inside SysEx
            wire = bytes.enumerated().flatMap { i, b in i % 4 == 3 ? [b, Proto.statusClock] : [b] }
        }
        if chunk > 0 {
            for start in stride(from: 0, to: wire.count, by: chunk) {
                output.yield(Array(wire[start..<min(wire.count, start + chunk)]))
            }
        } else {
            output.yield(wire)
        }
    }

    // MARK: firmware model (called with the lock held)

    private func handle(_ f: SysExFrame) -> [SysExFrame] {
        let me = state.deck
        func ack(_ c: UInt8) -> [SysExFrame] { [.ack(deck: me, command: c)] }
        func nak(_ c: UInt8, _ e: SysExError) -> [SysExFrame] { [.nak(deck: me, command: c, error: e)] }

        if let forced = state.forcedNak, forced.command == f.code {
            return [SysExFrame(deck: me, code: Proto.SysEx.nak, payload: [forced.command, forced.code])]
        }
        switch f.kind {
        case let .unknown(code) where code != Proto.SysEx.ack && code != Proto.SysEx.nak:
            return nak(code, .unknownCmd)
        case .command: break
        default: return []  // replies, ACKs and NAKs are not for us
        }
        let p = f.payload

        switch SysExCmd(rawValue: f.code)! {
        case .getInfo:
            var info = state.info
            info.deck = me
            return [SysExFrame(deck: me, reply: .info, payload: info.serialized())]

        case .getConfig:
            guard p.count == 1 else { return nak(f.code, .badLength) }
            if p[0] == 0xFF {
                return (0..<state.keyCount).map { configReply(UInt8($0)) }
            }
            guard Int(p[0]) < state.keyCount else { return nak(f.code, .badKey) }
            return [configReply(p[0])]

        case .setConfig:
            guard p.count == 5 else { return nak(f.code, .badLength) }
            guard Int(p[0]) < state.keyCount, let key = ConfigKey(rawValue: p[0]) else { return nak(f.code, .badKey) }
            let value = Int32(bitPattern: UInt32(p[1]) | UInt32(p[2]) << 8 | UInt32(p[3]) << 16 | UInt32(p[4]) << 24)
            let d = ConfigTable.desc(key)
            guard (d.minValue...d.maxValue).contains(value) else { return nak(f.code, .outOfRange) }
            state.config[Int(key.rawValue)] = value
            return ack(f.code)

        case .saveConfig:
            guard !state.failFlash else { return nak(f.code, .flashError) }
            state.saveCount += 1
            return ack(f.code)

        case .loadDefaults:
            state.config = ConfigTable.all.map(\.defaultValue)
            return ack(f.code)

        case .stream:
            guard p.count == 1 else { return nak(f.code, .badLength) }
            guard p[0] <= 100 else { return nak(f.code, .outOfRange) }
            state.streamRate = p[0]
            if p[0] > 0 { state.streamRefreshes += 1 }
            return ack(f.code)

        case .calBegin:
            guard p.count == 1 else { return nak(f.code, .badLength) }
            guard let kind = CalKind(rawValue: p[0]) else { return nak(f.code, .notSupported) }
            state.calKind = kind
            state.calMin = .max
            state.calMax = .min
            return ack(f.code)

        case .calEnd:
            guard p.count == 2 else { return nak(f.code, .badLength) }
            guard let kind = CalKind(rawValue: p[0]), state.calKind == kind else { return nak(f.code, .calNotActive) }
            let (lo, hi) = (state.calMin, state.calMax)
            state.calKind = nil
            guard hi != .min, hi - lo >= SweepCalibration.defaultMinSpan else { return nak(f.code, .calFailed) }
            let inset = (hi - lo) / 100
            let (outLo, outHi) = (lo + inset, hi - inset)
            if p[1] == 1 {
                let keys: (ConfigKey, ConfigKey) = kind == .fader ? (.faderRawMin, .faderRawMax) : (.knobRawMin, .knobRawMax)
                state.config[Int(keys.0.rawValue)] = Int32(outLo)
                state.config[Int(keys.1.rawValue)] = Int32(outHi)
            }
            let result = CalResult(kind: kind.rawValue, applied: p[1], rawMin: UInt16(outLo), rawMax: UInt16(outHi))
            return [SysExFrame(deck: me, reply: .calResult, payload: result.serialized())]

        case .setDeck:
            guard p.count == 1 else { return nak(f.code, .badLength) }
            guard Deck(rawValue: p[0]) != nil else { return nak(f.code, .outOfRange) }
            state.deck = p[0]
            // The contract does not say which id this ACK carries, so the fake can do either.
            return [.ack(deck: state.setDeckAcksWithNewId ? p[0] : me, command: f.code)]

        case .reboot:
            guard p.count == 1 else { return nak(f.code, .badLength) }
            state.lastReboot = p[0]
            return ack(f.code)

        case .linkStatus:
            return [SysExFrame(deck: me, reply: .link, payload: [UInt8(state.peers.count)] + state.peers.flatMap { $0.serialized() })]

        case .testTone:
            guard p.count == 4 else { return nak(f.code, .badLength) }
            guard state.hasDac else { return nak(f.code, .notSupported) }
            state.lastTone = p
            return ack(f.code)

        case .identify:
            guard p.count == 1 else { return nak(f.code, .badLength) }
            state.lastIdentify = p[0]
            return ack(f.code)
        }
    }

    private func configReply(_ key: UInt8) -> SysExFrame {
        let v = UInt32(bitPattern: state.config[Int(key)])
        let payload: [UInt8] = [key] + (0..<4).map { UInt8((v >> UInt32(8 * $0)) & 0xFF) }
        return SysExFrame(deck: state.deck, reply: .config, payload: payload)
    }
}

// Glue between the simulator process and the REAL Swift client libraries:
//   SimTransport  a MidiTransport for one USB-MIDI cable of one simulated unit. Its bytes cross the pipe as USB-MIDI
//                 packets, encoded and decoded by EPDJ3000Protocol.UsbMidi on this side and by the firmware's
//                 midi::encodePacket / encodeUsb on the other, so the two framing implementations meet here.
//   SimClock      a DeviceClock on the simulator's virtual time (request timeouts, the telemetry keep-alive).
//   SimWorld      owns the process, the clock and the clients, and advances virtual time in slices so every client has
//                 consumed everything the units sent before time moves on (the tests are deterministic).

import EPDJ3000Protocol
import Foundation
import XCTest

// MARK: - clock

final class SimClock: DeviceClock, @unchecked Sendable {
    private struct Sleeper {
        let id: Int
        let deadline: Double
        let continuation: CheckedContinuation<Void, Error>
    }

    private let lock = NSLock()
    private var now = 0.0
    private var nextId = 0
    private var sleepers: [Sleeper] = []

    var sleeperCount: Int { lock.withLock { sleepers.count } }

    /// Virtual time at which the next sleeper wants to wake, if any.
    var earliestDeadline: Double? { lock.withLock { sleepers.map(\.deadline).min() } }

    private func reserve(_ seconds: Double) -> (id: Int, deadline: Double) {
        lock.withLock {
            nextId += 1
            return (nextId, now + max(0, seconds))
        }
    }

    func sleep(seconds: Double) async throws {
        let (id, deadline) = reserve(seconds)
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
                let cancelled = lock.withLock { () -> Bool in
                    if Task.isCancelled { return true }
                    sleepers.append(Sleeper(id: id, deadline: deadline, continuation: continuation))
                    return false
                }
                if cancelled { continuation.resume(throwing: CancellationError()) }
            }
        } onCancel: {
            let mine = lock.withLock { () -> Sleeper? in
                let m = sleepers.first { $0.id == id }
                sleepers.removeAll { $0.id == id }
                return m
            }
            mine?.continuation.resume(throwing: CancellationError())
        }
    }

    /// Moves virtual time to `seconds` and wakes everything due. Returns how many sleepers woke.
    @discardableResult
    func advance(to seconds: Double) -> Int {
        let due = lock.withLock { () -> [Sleeper] in
            now = max(now, seconds)
            let due = sleepers.filter { $0.deadline <= now }
            sleepers.removeAll { $0.deadline <= now }
            return due
        }
        for s in due { s.continuation.resume() }
        return due.count
    }
}

// MARK: - recording a stream

/// Collects a stream on a background task so a test can read what has arrived without blocking.
final class Recorder<T: Sendable>: @unchecked Sendable {
    private let lock = NSLock()
    private var items: [T] = []
    private var received = 0
    private var task: Task<Void, Never>?

    init(_ stream: AsyncStream<T>) {
        task = Task { [weak self] in
            for await v in stream {
                guard let self else { return }
                self.lock.withLock {
                    self.items.append(v)
                    self.received += 1
                }
            }
        }
    }

    deinit { task?.cancel() }

    /// Items collected and not yet taken.
    var count: Int { lock.withLock { items.count } }

    /// Items received since the recorder started, whatever has been taken.
    var total: Int { lock.withLock { received } }

    var values: [T] { lock.withLock { items } }

    /// Returns what has been collected so far and forgets it.
    func take() -> [T] {
        lock.withLock {
            let out = items
            items = []
            return out
        }
    }
}

// MARK: - transport

final class SimTransport: MidiTransport, @unchecked Sendable {
    let incoming: AsyncStream<[UInt8]>
    let unit: Int
    let cable: Int
    private let continuation: AsyncStream<[UInt8]>.Continuation
    private let process: SimProcess
    private let lock = NSLock()
    private var telemetry = 0
    private var otherEvents = 0
    private var epdjReplies = 0

    init(process: SimProcess, unit: Int, cable: Int) {
        self.process = process
        self.unit = unit
        self.cable = cable
        var c: AsyncStream<[UInt8]>.Continuation!
        incoming = AsyncStream(bufferingPolicy: .unbounded) { c = $0 }
        continuation = c
        process.addEventHandler { [weak self] ev in self?.receive(ev) }
    }

    /// EP-DJ3000 telemetry frames delivered so far.
    var telemetryFrames: Int { locked { telemetry } }
    /// Everything else that is not an EP-DJ3000 SysEx reply (notes, CCs, clock, foreign SysEx).
    var eventMessages: Int { locked { otherEvents } }

    private func locked<R>(_ body: () -> R) -> R { lock.withLock(body) }

    private func receive(_ ev: SimEvent) {
        guard ev.unit == unit, ev.ports & 1 != 0, ev.cable == cable, ev.kind == "msg" || ev.kind == "sysex" else { return }
        guard let usb = ev.usb else { return }
        let packets = UsbMidi.decode(usb)
        let isEpdj = ev.bytes.count > 6 && ev.bytes[0] == 0xF0 && Array(ev.bytes[1...3]) == SysExCodec.header
        locked {
            if !isEpdj { otherEvents += 1 } else if ev.bytes[6] == SysExReply.telemetry.rawValue { telemetry += 1 } else { epdjReplies += 1 }
        }
        for p in packets { continuation.yield(p.bytes) }  // one USB packet at a time, as a host sees it
    }

    func send(_ bytes: [UInt8]) async throws {
        var parser = MidiStreamParser()
        var packets = [UInt8]()
        for event in parser.feed(bytes) {
            guard let p = UsbMidi.encode(cable: UInt8(cable), message: event.bytes) else { throw SimError.failed("cannot frame \(event)") }
            packets += p
        }
        guard !packets.isEmpty else { return }
        _ = try await process.command("usbpkt u=\(unit) hex=\(SimProcess.hex(packets))")
    }
}

// MARK: - a client with its recorders

/// A DeviceClient on one cable of one unit, plus recorders on its streams so a test can read what arrived.
final class SimClient: @unchecked Sendable {
    let client: DeviceClient
    let transport: SimTransport
    let telemetry: Recorder<Telemetry>
    let events: Recorder<MidiEvent>

    init(client: DeviceClient, transport: SimTransport) {
        self.client = client
        self.transport = transport
        telemetry = Recorder(client.telemetry())
        events = Recorder(client.events())
    }

    /// Waits (real time, bounded) until the client has handed out every telemetry frame and event the unit sent.
    func settle(file: StaticString = #filePath, line: UInt = #line) async {
        let deadline = Date().addingTimeInterval(5)
        while telemetry.total < transport.telemetryFrames || events.total < transport.eventMessages {
            if Date() > deadline {
                XCTFail("client did not consume everything the unit sent: telemetry \(telemetry.total)/\(transport.telemetryFrames), "
                    + "events \(events.total)/\(transport.eventMessages)", file: file, line: line)
                return
            }
            try? await Task.sleep(nanoseconds: 100_000)
        }
    }
}

// MARK: - world

/// When a test last talked to the simulator, in real time.
final class LastActivity: @unchecked Sendable {
    private let lock = NSLock()
    private var last = Date()
    func touch() { lock.withLock { last = Date() } }
    var idleSeconds: Double { lock.withLock { Date().timeIntervalSince(last) } }
}

final class SimWorld: @unchecked Sendable {
    let process: SimProcess
    let clock = SimClock()
    private var clients: [SimClient] = []
    private(set) var virtualSeconds = 0.0
    private let lastActivity = LastActivity()
    private var idlePump: Task<Void, Never>?

    /// The touch ring learns its baseline for 250 ms and settles for 3 s (touch_ring.h).
    static let settleMs = 3600

    static func make(_ args: String = "units=1", settle: Bool = true) async throws -> SimWorld {
        guard let binary = SimBinary.path() else { throw XCTSkip("epdj_engine_sim is not built; run tools/e2e.sh (or set EPDJ_ENGINE_SIM)") }
        let world = SimWorld(process: try SimProcess(binary: binary))
        let r = try await world.process.command("init \(args)")
        world.advanceClock(to: r.done.t)
        if settle { try await world.run(ms: settleMs) }
        return world
    }

    private init(process: SimProcess) {
        self.process = process
        process.activityHook = { [lastActivity] in lastActivity.touch() }
        // A request the unit never answers waits for a virtual timeout that only moves when a test moves time. If the whole
        // test sits idle in real time, let the timeout happen so a missing reply is a failure, not a hang.
        idlePump = Task { [clock, lastActivity] in
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 300_000_000)
                if lastActivity.idleSeconds > 1.0, let deadline = clock.earliestDeadline { clock.advance(to: deadline) }
            }
        }
    }

    func close(file: StaticString = #filePath, line: UInt = #line) {
        idlePump?.cancel()
        for c in clients { Task { await c.client.close() } }
        process.shutdown(file: file, line: line)
    }

    private func advanceClock(to micros: UInt64) {
        lastActivity.touch()
        virtualSeconds = Double(micros) / 1_000_000
        if clock.advance(to: virtualSeconds) > 0 {
            // woken sleepers (a keep-alive, a timeout) are tasks: give them a moment to put their request in the pipe
            Thread.sleep(forTimeInterval: 0.003)
        }
    }

    // MARK: clients

    /// A real DeviceClient on the virtual clock, with its own transport (a transport has a single consumer).
    /// `deck` is the address requests carry (127 = any).
    func client(unit: Int = 0, cable: Int = 0, deck: UInt8 = Proto.SysEx.deckAny, timeout: Double = 1.0,
                streamRefreshInterval: Double = 2.0) -> SimClient {
        let transport = SimTransport(process: process, unit: unit, cable: cable)
        let c = DeviceClient(transport: transport, deck: deck, clock: clock, timeout: timeout, streamRefreshInterval: streamRefreshInterval)
        let sc = SimClient(client: c, transport: transport)
        clients.append(sc)
        return sc
    }

    // MARK: driving the simulator

    @discardableResult
    func command(_ line: String) async throws -> SimReply {
        let r = try await process.command(line)
        advanceClock(to: r.done.t)
        return r
    }

    /// Advances virtual time in `slice`-ms steps; after each one every client has consumed what the units sent.
    func run(ms: Int, slice: Int = 50) async throws {
        var left = ms
        while left > 0 {
            let n = min(slice, left)
            let r = try await process.run(ms: n)
            advanceClock(to: r.done.t)
            for c in clients { await c.settle() }
            left -= n
        }
    }

    func ramp(_ what: String, from: Double, to: Double, ms: Int, unit: Int = 0, slice: Int = 50) async throws {
        var done = 0
        while done < ms {
            let len = min(slice, ms - done)
            let a = from + (to - from) * Double(done) / Double(ms)
            let b = from + (to - from) * Double(done + len) / Double(ms)
            let r = try await process.command("ramp u=\(unit) what=\(what) from=\(a) to=\(b) ms=\(len)")
            advanceClock(to: r.done.t)
            for c in clients { await c.settle() }
            done += len
        }
    }

    func finger(rev: Double? = nil, pressure: Double? = nil, unit: Int = 0) async throws {
        var line = "finger u=\(unit)"
        if let rev { line += " rev=\(rev)" }
        if let pressure { line += " pressure=\(pressure)" }
        try await command(line)
    }

    /// A finger lands at `startRev`, dwells, travels `revs` (negative = counter-clockwise), dwells and lifts.
    func gesture(startRev: Double = 0.25, revs: Double, ms: Int, dwellMs: Int = 100, tailMs: Int = 150, afterMs: Int = 300,
                 unit: Int = 0) async throws {
        try await finger(rev: startRev, pressure: 1, unit: unit)
        try await run(ms: dwellMs)
        try await ramp("finger", from: startRev, to: startRev + revs, ms: ms, unit: unit)
        try await run(ms: tailMs)
        try await finger(pressure: 0, unit: unit)
        try await run(ms: afterMs)
    }

    /// Waits (real time, bounded) until `count` tasks are asleep on the virtual clock, e.g. a request waiting for its timeout.
    func waitForSleepers(_ count: Int = 1, file: StaticString = #filePath, line: UInt = #line) async {
        let deadline = Date().addingTimeInterval(5)
        while clock.sleeperCount < count {
            if Date() > deadline { return XCTFail("nothing went to sleep on the virtual clock", file: file, line: line) }
            try? await Task.sleep(nanoseconds: 100_000)
        }
    }

    func status(unit: Int = 0) async throws -> SimDone {
        try await command("status u=\(unit)").done
    }

    /// Raw host MIDI to a unit (not through a DeviceClient): `port` is 0, 1, 2 or "trs".
    @discardableResult
    func midi(_ bytes: [UInt8], unit: Int = 0, port: String = "0") async throws -> SimReply {
        try await command("midi u=\(unit) port=\(port) hex=\(SimProcess.hex(bytes))")
    }

    private static let controlNotes: Set<UInt8> = [Proto.Note.cue, Proto.Note.rev, Proto.Note.touch, Proto.Note.play, Proto.Note.tap]
    private static let pairs: [(msb: UInt8, lsb: UInt8)] = Control14.allCases.map { ($0.msb, $0.lsb) }

    /// Wire rules that must hold for everything any unit ever sent (PROTOCOL.md 2 and 3); empty = they all hold:
    /// a unit's own 14-bit pairs go MSB before LSB within one engine tick (the radio bridge is skipped, it may deliver
    /// two updates in one tick), jog values are never 0 or 64 and come at most every 2 ms per channel, control notes
    /// go on with velocity 127 and are never turned on or off twice in a row, and every EP-DJ3000 SysEx frame decodes
    /// strictly with the Swift codec (checksum, version, 8->7 packing).
    func wireRuleViolations() -> [String] {
        var bad = [String]()
        func add(_ e: SimEvent, _ why: String) {
            if bad.count < 10 { bad.append("t=\(e.t) unit \(e.unit) cable \(e.cable) \(SimProcess.hex(e.bytes)): \(why)") }
        }
        var lastJog = [String: UInt64]()
        var noteOn = [String: Bool]()
        var lastHalf = [String: (t: UInt64, isLsb: Bool)]()
        for e in process.log {
            if e.kind == "sysex", e.bytes.count > 3, Array(e.bytes[1...3]) == SysExCodec.header {
                do { _ = try SysExFrame.decode(e.bytes) } catch { add(e, "SysEx does not decode: \(error)") }
            }
            // Cable 1 is the TRS jack passing external gear through; its traffic is not the unit's own.
            guard e.kind == "msg", e.ports & 1 != 0, e.cable != 1, e.bytes.count == 3, e.bytes[0] < 0xF0 else { continue }
            let status = e.bytes[0], d1 = e.bytes[1], d2 = e.bytes[2]
            if d1 > 0x7F || d2 > 0x7F { add(e, "data byte >= 0x80") }
            let key = "\(e.unit):\(e.cable):\(status & 0x0F)"
            switch status & 0xF0 {
            case 0xB0:
                if d1 == Proto.CC.jog {
                    if d2 == 0 || d2 == 64 { add(e, "jog value \(d2) must never be sent") }
                    if let prev = lastJog[key], e.t - prev < 2000 { add(e, "jog messages \(e.t - prev) us apart (>= 2000)") }
                    lastJog[key] = e.t
                }
                guard e.cable != 2, let pair = Self.pairs.first(where: { $0.msb == d1 || $0.lsb == d1 }) else { break }
                let pk = "\(key):\(pair.msb)"
                if d1 == pair.msb, let last = lastHalf[pk], last.t == e.t, last.isLsb { add(e, "MSB after LSB in one tick") }
                lastHalf[pk] = (e.t, d1 == pair.lsb)
            case 0x90, 0x80:
                let on = status & 0xF0 == 0x90 && d2 > 0
                if on, Self.controlNotes.contains(d1), d2 != 127 { add(e, "control note-on velocity \(d2)") }
                let nk = "\(key):\(d1)"
                if noteOn[nk] == on { add(e, on ? "note turned on twice" : "note turned off twice") }
                noteOn[nk] = on
            default:
                break
            }
        }
        return bad
    }

    /// The MIDI messages a unit sent on a cable since log index `index`, parsed with the Swift library, with their virtual time.
    func midiEvents(unit: Int = 0, cable: Int = 0, from index: Int = 0) -> [(t: UInt64, event: MidiEvent)] {
        var out = [(t: UInt64, event: MidiEvent)]()
        for e in events(unit: unit, cable: cable, from: index) where e.kind == "msg" || e.kind == "sysex" {
            var parser = MidiStreamParser()
            for ev in parser.feed(e.bytes) { out.append((e.t, ev)) }
        }
        return out
    }

    private static let faderControllers: Set<UInt8> = [
        Proto.CC.channelVolumeMsb, Proto.CC.channelVolumeLsb, Proto.CC.crossfaderMsb, Proto.CC.crossfaderLsb,
        Proto.CC.pitchMsb, Proto.CC.pitchLsb,
    ]

    /// Every value the slide pot's 14-bit control passed through, in the order a receiver sees it, for messages from log
    /// index `mark` on. The whole log is folded so the half a unit skipped as unchanged is known.
    func faderValues(unit: Int = 0, channel: UInt8 = 0, since mark: Int = 0) -> [Int] {
        var state = ControllerState(channel: channel)
        var values = [Int]()
        for (i, e) in process.log.enumerated() where e.unit == unit && e.ports & 1 != 0 && e.cable == 0 && e.kind == "msg" {
            var parser = MidiStreamParser()
            for ev in parser.feed(e.bytes) {
                state.apply(ev)
                guard i >= mark, case let .controlChange(_, cc, _) = ev, Self.faderControllers.contains(cc) else { continue }
                values.append(Int(state.fader))
            }
        }
        return values
    }

    /// Every USB-bound output whose 4-byte packets (built by the firmware's encodeUsb) do not decode, with the Swift
    /// UsbMidi library, to the message bytes the firmware says it built, on the cable it says. Empty = they agree.
    func framingDisagreements() -> [String] {
        var bad = [String]()
        for ev in process.log where ev.ports & 1 != 0 && (ev.kind == "msg" || ev.kind == "sysex") {
            guard let usb = ev.usb else {
                bad.append("t=\(ev.t) \(SimProcess.hex(ev.bytes)): no USB packets")
                continue
            }
            let packets = UsbMidi.decode(usb)
            if packets.flatMap(\.bytes) != ev.bytes || packets.contains(where: { Int($0.cable) != ev.cable }) {
                bad.append("t=\(ev.t) cable \(ev.cable) \(SimProcess.hex(ev.bytes)) vs usb \(SimProcess.hex(usb))")
            }
            if UsbMidi.encode(cable: UInt8(ev.cable), message: ev.bytes) != usb {
                bad.append("t=\(ev.t) cable \(ev.cable) \(SimProcess.hex(ev.bytes)): Swift UsbMidi.encode gives different packets than the firmware")
            }
        }
        return Array(bad.prefix(10))
    }

    /// All unit outputs matching a filter, in order.
    func events(unit: Int = 0, cable: Int = 0, from index: Int = 0) -> [SimEvent] {
        process.log.dropFirst(index).filter { $0.unit == unit && $0.ports & 1 != 0 && $0.cable == cable }
    }
}

// MARK: - test case base

class E2ETestCase: XCTestCase {
    private var watchdog: DispatchWorkItem?

    override func setUp() async throws {
        continueAfterFailure = false
        if SimBinary.path() == nil { throw XCTSkip("epdj_engine_sim is not built; run tools/e2e.sh (or set EPDJ_ENGINE_SIM)") }
        // Virtual time only moves when a test moves it, so a test that waits for the wrong thing would hang forever.
        let name = self.name
        let item = DispatchWorkItem {
            fputs("E2E WATCHDOG: \(name) did not finish within 120 s; aborting the run\n", stderr)
            exit(70)
        }
        watchdog = item
        DispatchQueue.global().asyncAfter(deadline: .now() + 120, execute: item)
    }

    override func tearDown() async throws {
        watchdog?.cancel()
        watchdog = nil
    }

    /// Runs `body` against a fresh simulator and fails if the simulator or its sanitizers complain.
    func withWorld(_ args: String = "units=1", settle: Bool = true, file: StaticString = #filePath, line: UInt = #line,
                   _ body: (SimWorld) async throws -> Void) async throws {
        let world = try await SimWorld.make(args, settle: settle)
        defer { world.close(file: file, line: line) }
        do {
            try await body(world)
        } catch {
            XCTFail("\(error)\nsimulator stderr:\n\(world.process.stderrOutput)", file: file, line: line)
            throw error
        }
        XCTAssertEqual(world.framingDisagreements(), [], "firmware encodeUsb and Swift UsbMidi.decode disagree", file: file, line: line)
        XCTAssertEqual(world.wireRuleViolations(), [], "the firmware broke a wire rule of PROTOCOL.md", file: file, line: line)
    }
}

/// `XCTAssertEqual` for an expression that needs `try await` (the XCTest autoclosures here cannot suspend).
func expectEqual<T: Equatable>(_ expression: @autoclosure () async throws -> T, _ expected: @autoclosure () -> T,
                               _ message: @autoclosure () -> String = "", file: StaticString = #filePath,
                               line: UInt = #line) async rethrows {
    let value = try await expression()
    XCTAssertEqual(value, expected(), message(), file: file, line: line)
}

/// A disagreement between two implementations that is understood, reported (docs/E2E.md) and owned by someone else.
/// While `holds` is false the test notes it and carries on; once it holds the test FAILS, so the marker gets promoted
/// to a plain assertion instead of rotting.
func knownIssue(_ id: String, _ what: String, holds: Bool, file: StaticString = #filePath, line: UInt = #line) {
    if holds {
        XCTFail("known issue \(id) (\(what)) no longer reproduces: turn it into a plain assertion and remove it from docs/E2E.md",
                file: file, line: line)
    } else {
        fputs("KNOWN ISSUE \(id): \(what)\n", stderr)
    }
}

/// Numbers worth reading in the test log (they are also summarised in docs/E2E.md), printed, never asserted.
func observe(_ message: String) {
    fputs("OBSERVATION: \(message)\n", stderr)
}

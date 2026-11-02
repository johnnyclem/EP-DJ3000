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
    private var task: Task<Void, Never>?

    init(_ stream: AsyncStream<T>) {
        task = Task { [weak self] in
            for await v in stream {
                guard let self else { return }
                self.lock.withLock { self.items.append(v) }
            }
        }
    }

    deinit { task?.cancel() }

    var count: Int { lock.withLock { items.count } }

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
        while telemetry.count < transport.telemetryFrames || events.count < transport.eventMessages {
            if Date() > deadline {
                XCTFail("client did not consume everything the unit sent: telemetry \(telemetry.count)/\(transport.telemetryFrames), "
                    + "events \(events.count)/\(transport.eventMessages)", file: file, line: line)
                return
            }
            try? await Task.sleep(nanoseconds: 100_000)
        }
    }
}

// MARK: - world

final class SimWorld: @unchecked Sendable {
    let process: SimProcess
    let clock = SimClock()
    private var clients: [SimClient] = []
    private var transports: [Int: SimTransport] = [:]
    private(set) var virtualSeconds = 0.0

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

    private init(process: SimProcess) { self.process = process }

    func close(file: StaticString = #filePath, line: UInt = #line) {
        for c in clients { Task { await c.client.close() } }
        process.shutdown(file: file, line: line)
    }

    private func advanceClock(to micros: UInt64) {
        virtualSeconds = Double(micros) / 1_000_000
        if clock.advance(to: virtualSeconds) > 0 {
            // woken sleepers (a keep-alive, a timeout) are tasks: give them a moment to put their request in the pipe
            Thread.sleep(forTimeInterval: 0.003)
        }
    }

    // MARK: clients

    func transport(unit: Int = 0, cable: Int = 0) -> SimTransport {
        let key = unit * 16 + cable
        if let t = transports[key] { return t }
        let t = SimTransport(process: process, unit: unit, cable: cable)
        transports[key] = t
        return t
    }

    /// A real DeviceClient on the virtual clock. `deck` is the address requests carry (127 = any).
    func client(unit: Int = 0, cable: Int = 0, deck: UInt8 = Proto.SysEx.deckAny, timeout: Double = 1.0,
                streamRefreshInterval: Double = 2.0) -> SimClient {
        let transport = transport(unit: unit, cable: cable)
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

    func status(unit: Int = 0) async throws -> SimDone {
        try await command("status u=\(unit)").done
    }

    /// Raw host MIDI to a unit (not through a DeviceClient): `port` is 0, 1, 2 or "trs".
    @discardableResult
    func midi(_ bytes: [UInt8], unit: Int = 0, port: String = "0") async throws -> SimReply {
        try await command("midi u=\(unit) port=\(port) hex=\(SimProcess.hex(bytes))")
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
    override func setUp() async throws {
        continueAfterFailure = false
        if SimBinary.path() == nil { throw XCTSkip("epdj_engine_sim is not built; run tools/e2e.sh (or set EPDJ_ENGINE_SIM)") }
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
    }
}

/// `XCTAssertEqual` for an expression that needs `try await` (the XCTest autoclosures here cannot suspend).
func expectEqual<T: Equatable>(_ expression: @autoclosure () async throws -> T, _ expected: @autoclosure () -> T,
                               _ message: @autoclosure () -> String = "", file: StaticString = #filePath,
                               line: UInt = #line) async rethrows {
    let value = try await expression()
    XCTAssertEqual(value, expected(), message(), file: file, line: line)
}

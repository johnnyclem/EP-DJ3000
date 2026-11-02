// The end-to-end simulator (firmware/core/tools/engine_sim.cpp, docs/E2E.md) as a child process.
//
// The real firmware Engine runs inside it; this file is only the pipe: text commands out, JSON event lines in.
// Everything the units send is delivered, in order, to `onEvent` on the reader thread BEFORE the command that
// caused it completes, so once `await command(...)` returns, every reply is already in the transports' buffers.

import EPDJ3000Protocol
import Foundation
import XCTest

/// One thing a simulated unit sent (`{"e":"out",...}`).
struct SimEvent: Sendable {
    var t: UInt64            // virtual microseconds
    var unit: Int
    var kind: String         // msg | sysex | bytes | link
    var ports: Int           // clockPorts mask: 1 = USB, 2 = TRS
    var cable: Int
    var bytes: [UInt8]       // the message as the firmware built it
    var usb: [UInt8]?        // the same, as the 4-byte USB-MIDI packets the firmware's encodeUsb produced

    var seconds: Double { Double(t) / 1_000_000 }
}

/// The terminating `{"e":"done",...}` line: virtual time and, for per-unit commands, the unit's state.
struct SimDone: @unchecked Sendable {
    let raw: [String: Any]

    var t: UInt64 { (raw["t"] as? NSNumber)?.uint64Value ?? 0 }
    func int(_ key: String) -> Int { (raw[key] as? NSNumber)?.intValue ?? Int.min }
    func bool(_ key: String) -> Bool { (raw[key] as? NSNumber)?.boolValue ?? false }
    func ints(_ key: String) -> [Int] { (raw[key] as? [NSNumber])?.map(\.intValue) ?? [] }
    func object(_ key: String) -> [String: Any] { raw[key] as? [String: Any] ?? [:] }

    var jogTotal: Int { int("jogTotal") }
    var transport: Int { int("transport") }
    var mode: Int { int("mode") }
    var tempoX10: Int { int("tempoX10") }
    var led: SimLed { SimLed(raw: object("led")) }
    /// Live config values by key id.
    var cfg: [Int] { ints("cfg") }
    /// What the non-volatile store holds.
    var stored: [Int] { ints("stored") }
}

struct SimLed: @unchecked Sendable {
    let raw: [String: Any]
    func int(_ key: String) -> Int { (raw[key] as? NSNumber)?.intValue ?? Int.min }
    func bool(_ key: String) -> Bool { (raw[key] as? NSNumber)?.boolValue ?? false }
    var cueLit: Bool { bool("cueLit") }
    var touched: Bool { bool("touched") }
    var transport: Int { int("transport") }
    var playing: Bool { transport == Int(TransportState.playing.rawValue) }
}

struct SimReply: Sendable {
    var events: [SimEvent]
    /// `{"e":"platform",...}` effects the engine asked its platform for (save, reboot, setDeck, tone).
    var platform: [String]
    var done: SimDone
}

enum SimError: Error, CustomStringConvertible {
    case notBuilt
    case exited(String)
    case failed(String)

    var description: String {
        switch self {
        case .notBuilt: return "epdj_engine_sim is not built"
        case let .exited(s): return "simulator exited: \(s)"
        case let .failed(s): return "simulator: \(s)"
        }
    }
}

enum SimBinary {
    /// tools/e2e.sh exports EPDJ_ENGINE_SIM; without it the default build directory is tried.
    static func path() -> String? {
        var candidates: [String] = []
        if let p = ProcessInfo.processInfo.environment["EPDJ_ENGINE_SIM"], !p.isEmpty { candidates.append(p) }
        let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        candidates.append(repo.appendingPathComponent("firmware/core/build-e2e/epdj_engine_sim").path)
        return candidates.first { FileManager.default.isExecutableFile(atPath: $0) }
    }
}

final class SimProcess: @unchecked Sendable {
    private let process = Process()
    private let toChild = Pipe()
    private let fromChild = Pipe()
    private let errFromChild = Pipe()
    private let lock = NSLock()
    private var pending: [CheckedContinuation<SimReply, Error>] = []
    private var currentEvents: [SimEvent] = []
    private var currentPlatform: [String] = []
    private var exitMessage: String?
    private var stderrText = ""
    private let ready = DispatchSemaphore(value: 0)
    private var onEvent: [(SimEvent) -> Void] = []

    private var logStorage: [SimEvent] = []

    /// Every unit output since launch, in order.
    var log: [SimEvent] {
        lock.lock()
        defer { lock.unlock() }
        return logStorage
    }

    init(binary: String) throws {
        process.executableURL = URL(fileURLWithPath: binary)
        process.standardInput = toChild
        process.standardOutput = fromChild
        process.standardError = errFromChild
        try process.run()

        let reader = Thread { [self] in readLoop() }
        reader.name = "epdj-sim-stdout"
        reader.start()
        let errReader = Thread { [self] in
            while true {
                let d = errFromChild.fileHandleForReading.availableData
                if d.isEmpty { break }
                lock.lock()
                stderrText += String(decoding: d, as: UTF8.self)
                lock.unlock()
            }
        }
        errReader.start()
        if ready.wait(timeout: .now() + 10) == .timedOut { throw SimError.failed("no ready line within 10 s") }
    }

    deinit { terminate() }

    /// Called on the reader thread for every unit output, before the command that provoked it completes.
    func addEventHandler(_ handler: @escaping (SimEvent) -> Void) {
        lock.lock()
        onEvent.append(handler)
        lock.unlock()
    }

    var stderrOutput: String {
        lock.lock()
        defer { lock.unlock() }
        return stderrText
    }

    // MARK: commands

    /// Sends one command line and returns when the simulator has answered it. Safe to call concurrently:
    /// the simulator processes lines in order and answers in order.
    func command(_ line: String) async throws -> SimReply {
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<SimReply, Error>) in
            lock.lock()
            if let m = exitMessage {
                lock.unlock()
                cont.resume(throwing: SimError.exited(m))
                return
            }
            pending.append(cont)
            do {
                try toChild.fileHandleForWriting.write(contentsOf: Data((line + "\n").utf8))
                lock.unlock()
            } catch {
                pending.removeLast()
                lock.unlock()
                cont.resume(throwing: SimError.failed("could not write to the simulator: \(error)"))
            }
        }
    }

    @discardableResult
    func run(ms: Int) async throws -> SimReply { try await command("run ms=\(ms)") }

    func status(unit: Int = 0) async throws -> SimDone { try await command("status u=\(unit)").done }

    /// Ends the child and reports (as test failures) a non-zero exit, which is how ASan/UBSan/LSan findings surface.
    func shutdown(file: StaticString = #filePath, line: UInt = #line) {
        guard process.isRunning else { return }
        try? toChild.fileHandleForWriting.write(contentsOf: Data("quit\n".utf8))
        process.waitUntilExit()
        if process.terminationStatus != 0 {
            XCTFail("simulator exited with status \(process.terminationStatus)\n\(stderrOutput)", file: file, line: line)
        }
    }

    private func terminate() {
        if process.isRunning {
            try? toChild.fileHandleForWriting.write(contentsOf: Data("quit\n".utf8))
            process.terminate()
        }
    }

    // MARK: reading

    private func readLoop() {
        var buffer = Data()
        while true {
            let chunk = fromChild.fileHandleForReading.availableData
            if chunk.isEmpty { break }
            buffer.append(chunk)
            while let nl = buffer.firstIndex(of: 0x0A) {
                let lineData = buffer[buffer.startIndex..<nl]
                buffer.removeSubrange(buffer.startIndex...nl)
                handle(line: lineData)
            }
        }
        process.waitUntilExit()
        lock.lock()
        let message = "status \(process.terminationStatus)\n\(stderrText)"
        exitMessage = message
        let waiting = pending
        pending = []
        lock.unlock()
        for c in waiting { c.resume(throwing: SimError.exited(message)) }
    }

    private func handle(line: Data) {
        guard let obj = (try? JSONSerialization.jsonObject(with: line)) as? [String: Any], let e = obj["e"] as? String else {
            fputs("simulator printed a line that is not a JSON event: \(String(decoding: line, as: UTF8.self))\n", stderr)
            return
        }
        switch e {
        case "ready":
            ready.signal()
        case "out":
            let ev = SimEvent(
                t: (obj["t"] as? NSNumber)?.uint64Value ?? 0, unit: (obj["u"] as? NSNumber)?.intValue ?? 0,
                kind: obj["k"] as? String ?? "", ports: (obj["ports"] as? NSNumber)?.intValue ?? 0,
                cable: (obj["cable"] as? NSNumber)?.intValue ?? 0, bytes: Self.fromHex(obj["hex"] as? String ?? ""),
                usb: (obj["usb"] as? String).map(Self.fromHex))
            lock.lock()
            logStorage.append(ev)
            currentEvents.append(ev)
            let handlers = onEvent
            lock.unlock()
            for h in handlers { h(ev) }
        case "platform":
            lock.lock()
            currentPlatform.append(String(decoding: line, as: UTF8.self))
            lock.unlock()
        case "done", "error":
            lock.lock()
            let events = currentEvents
            let platform = currentPlatform
            currentEvents = []
            currentPlatform = []
            let cont = pending.isEmpty ? nil : pending.removeFirst()
            lock.unlock()
            if e == "error" {
                cont?.resume(throwing: SimError.failed(obj["msg"] as? String ?? "unknown error"))
            } else {
                cont?.resume(returning: SimReply(events: events, platform: platform, done: SimDone(raw: obj)))
            }
        default:
            break
        }
    }

    static func fromHex(_ s: String) -> [UInt8] {
        var out = [UInt8]()
        out.reserveCapacity(s.utf8.count / 2)
        var high: UInt8?
        for c in s.utf8 {
            let v: UInt8
            switch c {
            case 48...57: v = c - 48
            case 97...102: v = c - 87
            case 65...70: v = c - 55
            default: continue
            }
            if let h = high {
                out.append(h << 4 | v)
                high = nil
            } else {
                high = v
            }
        }
        return out
    }

    static func hex(_ bytes: [UInt8]) -> String {
        bytes.map { String(format: "%02x", $0) }.joined()
    }
}

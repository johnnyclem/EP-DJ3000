import Foundation
import XCTest
@testable import EPDJ3000Protocol

/// Deterministic `DeviceClock`: time only moves when the test calls `advance(by:)`.
final class ManualClock: DeviceClock, @unchecked Sendable {
    private struct Sleeper {
        let id: Int
        let deadline: Double
        let continuation: CheckedContinuation<Void, Error>
    }

    private let lock = NSLock()
    private var now = 0.0
    private var nextId = 0
    private var sleepers: [Sleeper] = []

    var sleeperCount: Int {
        lock.lock()
        defer { lock.unlock() }
        return sleepers.count
    }

    func sleep(seconds: Double) async throws {
        let (id, deadline) = reserve(seconds)
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
                lock.lock()
                if Task.isCancelled {
                    lock.unlock()
                    continuation.resume(throwing: CancellationError())
                    return
                }
                sleepers.append(Sleeper(id: id, deadline: deadline, continuation: continuation))
                lock.unlock()
            }
        } onCancel: {
            lock.lock()
            let mine = sleepers.first { $0.id == id }
            sleepers.removeAll { $0.id == id }
            lock.unlock()
            mine?.continuation.resume(throwing: CancellationError())
        }
    }

    private func reserve(_ seconds: Double) -> (id: Int, deadline: Double) {
        lock.lock()
        defer { lock.unlock() }
        nextId += 1
        return (nextId, now + seconds)
    }

    func advance(by seconds: Double) {
        lock.lock()
        now += seconds
        let due = sleepers.filter { $0.deadline <= now }
        sleepers.removeAll { $0.deadline <= now }
        lock.unlock()
        for s in due { s.continuation.resume() }
    }
}

/// Polls (real time, 1 ms steps) until `condition` holds. Fails the test instead of hanging.
func waitUntil(_ what: String, timeout: TimeInterval = 5, file: StaticString = #filePath, line: UInt = #line,
               _ condition: () async -> Bool) async {
    let deadline = Date().addingTimeInterval(timeout)
    while await !condition() {
        if Date() > deadline {
            XCTFail("timed out waiting for \(what)", file: file, line: line)
            return
        }
        try? await Task.sleep(nanoseconds: 1_000_000)
    }
}

/// Collects up to `count` values from a stream, giving up after `timeout` seconds of real time.
func collect<T: Sendable>(_ stream: AsyncStream<T>, count: Int, timeout: TimeInterval = 5) async -> [T] {
    await withTaskGroup(of: [T]?.self) { group in
        group.addTask {
            var out = [T]()
            for await v in stream {
                out.append(v)
                if out.count == count { return out }
            }
            return out  // stream ended early
        }
        group.addTask {
            try? await Task.sleep(nanoseconds: UInt64(timeout * 1e9))
            return nil
        }
        let first = await group.next() ?? nil
        group.cancelAll()
        return first ?? []
    }
}

func XCTAssertThrowsDeviceError<T>(_ expected: DeviceError, file: StaticString = #filePath, line: UInt = #line,
                                   _ body: () async throws -> T) async {
    do {
        _ = try await body()
        XCTFail("expected \(expected), but the call succeeded", file: file, line: line)
    } catch let e as DeviceError {
        XCTAssertEqual(e, expected, file: file, line: line)
    } catch {
        XCTFail("expected \(expected), got \(error)", file: file, line: line)
    }
}

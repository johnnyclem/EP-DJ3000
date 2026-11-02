import Foundation
import XCTest
@testable import EPDJ3000KitPure
import EPDJ3000Protocol

final class MidiLogTests: XCTestCase {
    private func note(_ n: UInt8 = 36) -> MidiEvent { .noteOn(channel: 0, note: n, velocity: 127) }

    func testAppendAndRecent() {
        let log = MidiLog()
        for i in 0..<10 { log.append(source: "a", direction: .rx, event: note(UInt8(i)), time: Double(i)) }
        XCTAssertEqual(log.count, 10)
        let last3 = log.recent(limit: 3)
        XCTAssertEqual(last3.map(\.time), [7, 8, 9], "newest three, oldest first")
        let evens = log.recent(limit: 100) { $0.time.truncatingRemainder(dividingBy: 2) == 0 }
        XCTAssertEqual(evens.map(\.time), [0, 2, 4, 6, 8])
    }

    func testClockTicksAreFolded() {
        let log = MidiLog()
        for i in 0..<24 { log.append(source: "a", direction: .rx, event: .clock, time: Double(i)) }
        XCTAssertEqual(log.count, 1)
        XCTAssertEqual(log.recent(limit: 5)[0].repeatCount, 24)
        log.append(source: "a", direction: .rx, event: note(), time: 30)
        log.append(source: "a", direction: .rx, event: .clock, time: 31)
        XCTAssertEqual(log.count, 3, "a clock after another message starts a new entry")
        log.append(source: "b", direction: .rx, event: .clock, time: 32)
        XCTAssertEqual(log.count, 4, "clock from another source is not folded in")
        log.append(source: "b", direction: .tx, event: .clock, time: 33)
        XCTAssertEqual(log.count, 5, "nor is the other direction")
    }

    func testPauseDropsAndResumeKeeps() {
        let log = MidiLog()
        log.append(source: "a", direction: .rx, event: note(), time: 0)
        log.isPaused = true
        log.append(source: "a", direction: .rx, event: note(), time: 1)
        XCTAssertEqual(log.count, 1)
        log.isPaused = false
        log.append(source: "a", direction: .rx, event: note(), time: 2)
        XCTAssertEqual(log.count, 2)
        log.clear()
        XCTAssertEqual(log.count, 0)
    }

    func testRawBytesAreParsed() {
        let log = MidiLog()
        log.append(source: "a", direction: .tx, bytes: [0x90, 39, 127, 0x90, 36, 0], time: 5)
        let entries = log.recent(limit: 10)
        XCTAssertEqual(entries.count, 2)
        XCTAssertEqual(entries[0].event, .noteOn(channel: 0, note: 39, velocity: 127))
        XCTAssertEqual(entries[1].event, .noteOff(channel: 0, note: 36, velocity: 0))
        XCTAssertEqual(entries[0].direction, .tx)
    }

    func testCapacityTrimsOldest() {
        let log = MidiLog()
        let total = MidiLog.capacity + 4_000 + 10
        for i in 0..<total { log.append(source: "a", direction: .rx, event: note(UInt8(i % 100)), time: Double(i)) }
        XCTAssertLessThanOrEqual(log.count, MidiLog.capacity + 4_000)
        XCTAssertGreaterThanOrEqual(log.count, MidiLog.capacity)
        XCTAssertEqual(log.recent(limit: 1)[0].time, Double(total - 1), "newest survives")
    }

    func testIdsAreUniqueAndIncreasing() {
        let log = MidiLog()
        for i in 0..<50 { log.append(source: "a", direction: .rx, event: note(), time: Double(i)) }
        let ids = log.recent(limit: 50).map(\.id)
        XCTAssertEqual(ids, Array(ids.min()!...ids.max()!))
    }

    func testConcurrentAppendsAreSafe() {
        let log = MidiLog()
        DispatchQueue.concurrentPerform(iterations: 8) { worker in
            for i in 0..<2_000 {
                log.append(source: "w\(worker)", direction: .rx, event: .noteOn(channel: 0, note: UInt8(i % 100), velocity: 1), time: Double(i))
                if i % 100 == 0 { _ = log.recent(limit: 20) }
            }
        }
        XCTAssertEqual(log.count, 16_000)
        XCTAssertEqual(Set(log.recent(limit: 16_000).map(\.id)).count, 16_000)
    }

    func testNames() {
        let log = MidiLog()
        XCTAssertEqual(log.name(forSource: "x"), "x")
        log.setName("Deck A", forSource: "x")
        XCTAssertEqual(log.name(forSource: "x"), "Deck A")
    }
}

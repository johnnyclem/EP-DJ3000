import Foundation
import XCTest
@testable import EPDJ3000KitPure
import EPDJ3000Protocol

final class TestSignalTests: XCTestCase {
    func testSine() {
        let s = TestSignal.sine(frequency: 440, seconds: 1, amplitude: 0.5, sampleRate: 48_000)
        XCTAssertEqual(s.left.count, 48_000)
        XCTAssertEqual(s.left, s.right)
        XCTAssertEqual(s.left.max()!, 0.5, accuracy: 0.001)
        XCTAssertEqual(s.left[0], 0)
        // 440 zero crossings upward in one second.
        var crossings = 0
        for i in 1..<s.left.count where s.left[i - 1] < 0 && s.left[i] >= 0 { crossings += 1 }
        XCTAssertEqual(crossings, 440, accuracy: 1)
    }

    func testDemoLoopShapeAndDeterminism() {
        let a = TestSignal.demoLoop(bpm: 120, bars: 2)
        let b = TestSignal.demoLoop(bpm: 120, bars: 2)
        XCTAssertEqual(a.left.count, 2 * 4 * 24_000, "2 bars of 4 beats at 120 BPM, 48 kHz")
        XCTAssertEqual(a.left, b.left)
        XCTAssertEqual(a.right, b.right)
        XCTAssertTrue(a.left.allSatisfy { $0.isFinite && abs($0) <= 1 })
        XCTAssertGreaterThan(a.left[0..<4_800].map { abs($0) }.max()!, 0.3, "a kick lands on the downbeat")
        XCTAssertEqual(a.left[0], 0)
        XCTAssertNotEqual(a.left, a.right, "the arpeggio is panned")
    }

    func testDemoLoopHasDynamics() {
        // Not a constant tone: the level over 20 ms windows must move a lot, or scratching it would sound like nothing.
        let a = TestSignal.demoLoop(bpm: 120, bars: 1)
        let window = 960
        let levels = stride(from: 0, to: a.left.count - window, by: window).map { start -> Float in
            sqrt(a.left[start..<(start + window)].reduce(0) { $0 + $1 * $1 } / Float(window))
        }
        XCTAssertGreaterThan(levels.max()!, levels.min()! * 2)
    }

    func testDegenerateArguments() {
        XCTAssertTrue(TestSignal.sine(seconds: 0).left.isEmpty)
        XCTAssertFalse(TestSignal.demoLoop(bars: 0).left.isEmpty, "bars is clamped to at least one")
    }
}

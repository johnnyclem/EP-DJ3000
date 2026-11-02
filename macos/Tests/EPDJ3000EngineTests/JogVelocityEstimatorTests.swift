import XCTest
@testable import EPDJ3000Engine

final class JogVelocityEstimatorTests: XCTestCase {
    private let nominal = ScratchConstants.nominalTicksPerSecond

    private var carry = 0.0

    /// A platter turning at `ticksPerSecond`, reported every 2 ms like the device (whole ticks, remainder carried).
    @discardableResult
    private func spin(_ e: inout JogVelocityEstimator, ticksPerSecond: Double, from t0: Double, seconds: Double) -> Double {
        var t = t0
        let end = t0 + seconds
        while t < end - 1e-12 {
            t += 0.002
            carry += ticksPerSecond * 0.002
            let whole = Int(carry)  // truncates toward zero
            carry -= Double(whole)
            if whole != 0 { e.addTicks(whole, at: t) }
        }
        return t
    }

    func testSteadySpinReadsItsSpeedWithinTheTickQuantisation() {
        var e = JogVelocityEstimator()
        let end = spin(&e, ticksPerSecond: nominal, from: 0, seconds: 0.5)
        // ~10 ticks over ~18 ms, so the one-tick quantisation is +-10 %.
        XCTAssertEqual(e.velocity(at: end), nominal, accuracy: nominal * 0.1)
        XCTAssertEqual(e.speed(at: end), 1.0, accuracy: 0.1)

        var sum = 0.0
        var n = 0
        var t = end
        for _ in 0..<100 {
            t = spin(&e, ticksPerSecond: nominal, from: t, seconds: 0.002)
            sum += e.speed(at: t)
            n += 1
        }
        XCTAssertEqual(sum / Double(n), 1.0, accuracy: 0.03, "unbiased on average, queried at event times")
    }

    func testDirectionIsTheSignOfTheTicks() {
        var e = JogVelocityEstimator()
        let end = spin(&e, ticksPerSecond: -2 * nominal, from: 0, seconds: 0.2)
        XCTAssertEqual(e.speed(at: end), -2.0, accuracy: 0.2)
    }

    func testAHeldStillFingerReadsZeroWithinThirtyMilliseconds() {
        var e = JogVelocityEstimator()
        let last = spin(&e, ticksPerSecond: 3 * nominal, from: 0, seconds: 0.3)
        XCTAssertGreaterThan(e.velocity(at: last), 2 * nominal)

        var previous = e.velocity(at: last)
        var t = last
        while t < last + 0.020 {
            t += 0.001
            let v = e.velocity(at: t)
            XCTAssertLessThanOrEqual(v, previous, "decays monotonically")
            previous = v
        }
        XCTAssertEqual(e.velocity(at: last + 0.0201), 0)
        XCTAssertEqual(e.velocity(at: last + 0.030), 0)
        XCTAssertEqual(e.velocity(at: last + 5), 0)
        XCTAssertEqual(e.velocity(at: last + 0.010), 3 * nominal / 2, accuracy: nominal, "about half-way through the window")
    }

    func testNoEventsAndResetReadZero() {
        var e = JogVelocityEstimator()
        XCTAssertEqual(e.velocity(at: 123), 0)
        e.addTicks(5, at: 1.0)
        XCTAssertEqual(e.velocity(at: 1.0), 5 / e.window)
        e.reset()
        XCTAssertEqual(e.velocity(at: 1.0), 0)
    }

    func testAClockThatGoesBackwardsStartsOver() {
        var e = JogVelocityEstimator()
        e.addTicks(10, at: 100)
        e.addTicks(1, at: 5)
        XCTAssertEqual(e.velocity(at: 5), 1 / e.window)
    }

    func testBurstsAtOneTimestampLoseNoTicks() {
        // 100 single-tick events at one instant (more than the ring holds) read the same as one 100-tick event.
        var many = JogVelocityEstimator()
        var one = JogVelocityEstimator()
        many.addTicks(0, at: 2.0)
        one.addTicks(0, at: 2.0)
        for _ in 0..<100 { many.addTicks(1, at: 2.002) }
        one.addTicks(100, at: 2.002)
        XCTAssertEqual(many.velocity(at: 2.002), one.velocity(at: 2.002))
        XCTAssertEqual(many.velocity(at: 2.002), 100 / (many.window / 2), "span floored at half a window: no spike")
        XCTAssertEqual(many.velocity(at: 2.002 + many.window), 0)
    }

    func testTheOldestEventIsOnlyATimeReference() {
        // 1 tick at t = 0, then 2 more at 0.010: 2 ticks in 10 ms, not 3.
        var e = JogVelocityEstimator()
        e.addTicks(1, at: 0)
        e.addTicks(2, at: 0.010)
        XCTAssertEqual(e.velocity(at: 0.010), 200, accuracy: 1e-9)
    }

    func testRateDoesNotDependOnWhereTheWindowEdgeFalls() {
        // Exactly periodic events are the worst case for a fixed window: one event in or out of it moves the
        // reading by 10 %. Shift the window length across the event spacing and the answer must not move.
        var readings = [Double]()
        for w in stride(from: 0.0191, through: 0.0231, by: 0.0004) {
            var e = JogVelocityEstimator(window: w)
            var last = 0.0
            for k in 1...60 { last = Double(k) * 0.002; e.addTicks(1, at: last) }  // exactly 500 ticks/s
            readings.append(e.velocity(at: last))
        }
        for r in readings { XCTAssertEqual(r, 500, accuracy: 1e-6) }
    }

    func testSignedTicksCancel() {
        var e = JogVelocityEstimator()
        e.addTicks(0, at: 0.998)
        e.addTicks(7, at: 1.000)
        e.addTicks(-7, at: 1.002)
        XCTAssertEqual(e.velocity(at: 1.002), 0)
    }

    func testIgnoresNonFiniteTimes() {
        var e = JogVelocityEstimator()
        e.addTicks(4, at: .nan)
        e.addTicks(4, at: .infinity)
        XCTAssertEqual(e.velocity(at: .nan), 0)
        XCTAssertEqual(e.velocity(at: 0), 0)
    }

    func testSlowSpinBelowOneTickPerWindowIsReportedAsBurstsNotInvented() {
        // The documented limit: 1 tick / 20 ms = 50 ticks/s. Slower than that reads 0 between ticks.
        var e = JogVelocityEstimator()
        e.addTicks(1, at: 1.0)
        XCTAssertEqual(e.velocity(at: 1.0), 50)
        XCTAssertEqual(e.velocity(at: 1.05), 0)
    }
}

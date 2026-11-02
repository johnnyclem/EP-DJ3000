import XCTest
@testable import EPDJ3000Protocol

final class SweepCalibrationTests: XCTestCase {
    private func fader(_ raw: Int) -> Telemetry { Telemetry(faderRaw: UInt16(raw)) }
    private func knob(_ raw: Int) -> Telemetry { Telemetry(knobRaw: UInt16(raw)) }

    /// A smooth sweep with +-3 count jitter, like the ADC noise the spec talks about.
    private func sweep(_ cal: inout SweepCalibration, from a: Int, to b: Int, steps: Int = 100, make: (Int) -> Telemetry) {
        for i in 0...steps {
            let raw = a + (b - a) * i / steps + (i % 3 - 1) * 3
            cal.feed(make(max(0, min(4095, raw))))
        }
    }

    func testAscendingFaderSweepGivesInsetRangeAndNoInvert() throws {
        var cal = SweepCalibration(kind: .fader)
        XCTAssertEqual(cal.phase, .waitingForMotion)
        sweep(&cal, from: 100, to: 3900) { fader($0) }
        XCTAssertEqual(cal.phase, .rangeReached)

        let r = try cal.finish()
        XCTAssertEqual(r.kind, .fader)
        XCTAssertEqual(r.rawMin, 97)
        XCTAssertEqual(r.rawMax, 3900)
        XCTAssertEqual(r.span, 3803)
        XCTAssertEqual(r.invert, false)
        // 1% of 3803 = 38 counts inset at each end
        XCTAssertEqual(r.configUpdates, [ConfigUpdate(key: .faderRawMin, value: 135),
                                         ConfigUpdate(key: .faderRawMax, value: 3862),
                                         ConfigUpdate(key: .faderInvert, value: 0)])
    }

    func testDescendingFaderSweepSuggestsInvert() throws {
        var cal = SweepCalibration(kind: .fader)
        sweep(&cal, from: 3900, to: 100) { fader($0) }
        let r = try cal.finish()
        XCTAssertEqual(r.invert, true)
        XCTAssertEqual(r.configUpdates.last, ConfigUpdate(key: .faderInvert, value: 1))
        XCTAssertLessThan(r.configUpdates[0].value, r.configUpdates[1].value, "min < max whatever the direction")
    }

    func testDirectionDoesNotDependOnWhereTheSweepStarted() throws {
        var cal = SweepCalibration(kind: .fader)
        sweep(&cal, from: 2000, to: 100) { fader($0) }   // starts mid-travel, goes low...
        sweep(&cal, from: 100, to: 3900) { fader($0) }   // ...then to the high end and stays
        XCTAssertEqual(try cal.finish().invert, false)
    }

    func testSweepThatEndsMidTravelLeavesDirectionUnknown() throws {
        var cal = SweepCalibration(kind: .fader)
        sweep(&cal, from: 100, to: 3900) { fader($0) }
        sweep(&cal, from: 3900, to: 2000) { fader($0) }
        let r = try cal.finish()
        XCTAssertNil(r.invert)
        XCTAssertEqual(r.configUpdates.map(\.key), [.faderRawMin, .faderRawMax], "no invert update when direction is unknown")
    }

    func testKnobUsesKnobReadingAndKnobKeys() throws {
        var cal = SweepCalibration(kind: .knob)
        sweep(&cal, from: 0, to: 4095) { knob($0) }
        let r = try cal.finish()
        XCTAssertEqual(r.kind, .knob)
        XCTAssertEqual(r.configUpdates.map(\.key), [.knobRawMin, .knobRawMax, .knobInvert])
        XCTAssertEqual(r.rawMin, 0)
        XCTAssertEqual(r.rawMax, 4095)
        XCTAssertEqual(r.configUpdates[0].value, 40)
        XCTAssertEqual(r.configUpdates[1].value, 4055)
    }

    func testKnobCalibrationIgnoresFaderMovement() {
        var cal = SweepCalibration(kind: .knob)
        for raw in stride(from: 0, through: 4000, by: 100) { cal.feed(Telemetry(faderRaw: UInt16(raw), knobRaw: 2000)) }
        XCTAssertEqual(cal.span, 0)
        XCTAssertEqual(cal.phase, .waitingForMotion)
    }

    func testPhasesAdvanceWithTravel() {
        var cal = SweepCalibration(kind: .fader)
        XCTAssertEqual(cal.span, 0)
        for raw in [2000, 2003, 1998, 2002] { cal.feed(fader(raw)) }
        XCTAssertEqual(cal.phase, .waitingForMotion, "ADC noise is not motion")
        cal.feed(fader(2300))
        XCTAssertEqual(cal.phase, .sweeping)
        cal.feed(fader(500))
        XCTAssertEqual(cal.phase, .rangeReached)
        XCTAssertEqual(cal.span, 1800)
    }

    func testTooLittleTravelFailsLikeTheDevice() {
        var cal = SweepCalibration(kind: .fader)
        sweep(&cal, from: 1000, to: 2400) { fader($0) }
        XCTAssertThrowsError(try cal.finish()) {
            guard case let CalibrationError.insufficientTravel(span, required) = $0 else { return XCTFail("\($0)") }
            XCTAssertEqual(required, 1500)
            XCTAssertLessThan(span, 1500)
        }
        XCTAssertThrowsError(try SweepCalibration(kind: .knob).finish(), "no samples at all")
    }

    func testInsetKeepsConfigInsideTheAdcRange() throws {
        var cal = SweepCalibration(kind: .fader)
        cal.feed(fader(0))
        cal.feed(fader(4095))
        let r = try cal.finish()
        for u in r.configUpdates { XCTAssertTrue((0...4095).contains(u.value), "\(u)") }
    }
}

final class TouchRingCalibrationTests: XCTestCase {
    private let rate = 50
    private let idleMean = 20_000.0

    /// Plays a scenario into the calibrator: per-sector rise in per-mille (nil = no finger), with p-p jitter.
    private func frame(rises: [Double?], jitter: Double = 0, step: Int = 0) -> Telemetry {
        var raw = [UInt16](repeating: 0, count: 8)
        for s in 0..<8 {
            let wobble = (step % 2 == 0 ? 0.5 : -0.5) * jitter
            raw[s] = UInt16((idleMean * (1 + ((rises.indices.contains(s) ? rises[s] ?? 0 : 0) + wobble) / 1000)).rounded())
        }
        return Telemetry(touchRaw: raw)
    }

    private func run(_ cal: inout TouchRingCalibration, rises: [Double?], samples: Int, jitter: Double = 0) {
        for i in 0..<samples { cal.feed(frame(rises: rises, jitter: jitter, step: i)) }
    }

    /// Idle for 1 s, then each sector in turn: hold a finger, then release.
    private func fullRun(_ cal: inout TouchRingCalibration, sectorRises: [Double], jitter: Double = 0) {
        let none = [Double?](repeating: nil, count: sectorRises.count)
        run(&cal, rises: none, samples: rate, jitter: jitter)
        for s in sectorRises.indices {
            var r = none
            run(&cal, rises: none, samples: 5, jitter: jitter)  // finger approaching
            r[s] = sectorRises[s]
            run(&cal, rises: r, samples: rate, jitter: jitter)  // hold
            run(&cal, rises: none, samples: 6, jitter: jitter)  // lift
        }
    }

    func testThresholdsAreFiftyAndThirtyPercentOfTheWeakestSector() throws {
        var cal = TouchRingCalibration(sectors: 8, rateHz: rate)
        // Sector 5 is the weakest at 40 per-mille; idle jitter is 2 per-mille p-p.
        fullRun(&cal, sectorRises: [90, 80, 70, 60, 50, 40, 100, 85], jitter: 2)
        XCTAssertEqual(cal.phase, .done)

        let r = try cal.finish()
        XCTAssertEqual(r.sectors, 8)
        XCTAssertEqual(r.onPermille, 20)   // 50% of 40
        XCTAssertEqual(r.offPermille, 12)  // 60% of 20
        XCTAssertEqual(r.noisePermille, 2, accuracy: 0.05)
        // The peak is the lower of two consecutive samples, so it reads half the jitter (1) under the true rise.
        XCTAssertEqual(r.risePermille[5], 39, accuracy: 0.1)
        XCTAssertEqual(r.risePermille[6], 99, accuracy: 0.1)
        XCTAssertEqual(r.idleMean[0], idleMean, accuracy: 1)
        XCTAssertEqual(r.configUpdates, [ConfigUpdate(key: .touchOnPermille, value: 20),
                                         ConfigUpdate(key: .touchOffPermille, value: 12)])
    }

    func testRatiosReproduceTheShippedDefaults() throws {
        // Defaults are on = 12, off = 7: a weakest-sector rise of 24 per-mille must give exactly that.
        var cal = TouchRingCalibration(sectors: 3, rateHz: rate)
        fullRun(&cal, sectorRises: [30, 24, 28])
        let r = try cal.finish()
        XCTAssertEqual(r.onPermille, 12)
        XCTAssertEqual(r.offPermille, 7)
    }

    func testNoiseFloorRaisesOffAndKeepsOnAboveIt() throws {
        var cal = TouchRingCalibration(sectors: 3, rateHz: rate)
        // 5 per-mille p-p noise: floor = 15. 50%/60% would give on 12 / off 7.
        fullRun(&cal, sectorRises: [60, 24, 70], jitter: 5)
        let r = try cal.finish()
        XCTAssertEqual(r.noisePermille, 5, accuracy: 0.05)
        XCTAssertEqual(r.offPermille, 15, "never below 3x idle noise")
        XCTAssertEqual(r.onPermille, 16, "on stays above off")
        XCTAssertLessThan(Double(r.onPermille), 24)
    }

    func testPhasesWalkTheWizard() {
        var cal = TouchRingCalibration(sectors: 3, rateHz: rate)
        XCTAssertEqual(cal.phase, .capturingIdle(collected: 0, needed: 50))
        let none = [Double?](repeating: nil, count: 3)
        run(&cal, rises: none, samples: 10)
        XCTAssertEqual(cal.phase, .capturingIdle(collected: 10, needed: 50))
        run(&cal, rises: none, samples: 40)
        XCTAssertEqual(cal.phase, .awaitingFinger(sector: 0))
        XCTAssertThrowsError(try cal.finish()) { XCTAssertEqual($0 as? CalibrationError, .notComplete) }

        run(&cal, rises: [40, nil, nil], samples: 1)
        guard case .holding(sector: 0, _, _) = cal.phase else { return XCTFail("\(cal.phase)") }
        run(&cal, rises: [40, nil, nil], samples: 30)
        XCTAssertEqual(cal.phase, .awaitingRelease(sector: 0))
        run(&cal, rises: [40, nil, nil], samples: 10)
        XCTAssertEqual(cal.phase, .awaitingRelease(sector: 0), "finger still down")
        run(&cal, rises: none, samples: 3)
        XCTAssertEqual(cal.phase, .awaitingFinger(sector: 1))
    }

    func testAnotherSectorTouchedFirstDoesNotCountForTheWaitedSector() {
        var cal = TouchRingCalibration(sectors: 3, rateHz: rate)
        run(&cal, rises: [nil, nil, nil], samples: 50)
        run(&cal, rises: [nil, 80, nil], samples: 20)
        XCTAssertEqual(cal.phase, .awaitingFinger(sector: 0))
    }

    func testOneSampleSpikeCannotSetThePeak() throws {
        var cal = TouchRingCalibration(sectors: 3, rateHz: rate)
        let none = [Double?](repeating: nil, count: 3)
        run(&cal, rises: none, samples: 50)
        for s in 0..<3 {
            var r = none
            r[s] = 40
            run(&cal, rises: r, samples: 20)
            var spike = none
            spike[s] = 400
            run(&cal, rises: spike, samples: 1)
            run(&cal, rises: r, samples: 10)
            run(&cal, rises: none, samples: 5)
        }
        let r = try cal.finish()
        XCTAssertEqual(r.risePermille[0], 40, accuracy: 0.1)
        XCTAssertEqual(r.onPermille, 20)
    }

    // MARK: failures

    func testFingerOnPlatterDuringIdleFailsAsUnstable() {
        var cal = TouchRingCalibration(sectors: 3, rateHz: rate)
        run(&cal, rises: [nil, nil, nil], samples: 49)
        run(&cal, rises: [60, nil, nil], samples: 1)  // one sector jumped
        guard case .failed(.unstableIdle) = cal.phase else { return XCTFail("\(cal.phase)") }
        XCTAssertThrowsError(try cal.finish())
    }

    func testDisconnectedPadFailsAsDeadSector() {
        var cal = TouchRingCalibration(sectors: 3, rateHz: rate)
        for _ in 0..<50 { cal.feed(Telemetry(touchRaw: [20000, 0, 20000, 0, 0, 0, 0, 0])) }
        XCTAssertEqual(cal.phase, .failed(.deadSector(1)))
    }

    func testNoFingerWithinTimeoutNamesTheSector() {
        var cal = TouchRingCalibration(sectors: 3, rateHz: rate, fingerTimeoutSeconds: 2)
        let none = [Double?](repeating: nil, count: 3)
        run(&cal, rises: none, samples: 50)
        run(&cal, rises: none, samples: 99)
        XCTAssertEqual(cal.phase, .awaitingFinger(sector: 0))
        run(&cal, rises: none, samples: 1)
        XCTAssertEqual(cal.phase, .failed(.noFingerDetected(sector: 0)))
        XCTAssertThrowsError(try cal.finish()) { XCTAssertEqual($0 as? CalibrationError, .noFingerDetected(sector: 0)) }
    }

    func testTooWeakASectorFailsForContrast() {
        var cal = TouchRingCalibration(sectors: 3, rateHz: rate)
        // Weakest sector reads 11.5 per-mille at its low jitter phase, under 5 per-mille noise: the 3x noise floor
        // needs on >= 16, which the finger cannot reach.
        fullRun(&cal, sectorRises: [60, 14, 60], jitter: 5)
        XCTAssertEqual(cal.phase, .done)
        XCTAssertThrowsError(try cal.finish()) {
            guard case let CalibrationError.insufficientContrast(weakest, on) = $0 else { return XCTFail("\($0)") }
            XCTAssertEqual(weakest, 11)
            XCTAssertEqual(on, 16)
        }
    }

    func testFeedingAfterCompletionChangesNothing() throws {
        var cal = TouchRingCalibration(sectors: 3, rateHz: rate)
        fullRun(&cal, sectorRises: [40, 40, 40])
        let before = try cal.finish()
        run(&cal, rises: [500, 500, 500], samples: 100)
        XCTAssertEqual(try cal.finish(), before)
    }

    func testSectorCountIsClampedToTheHardware() {
        XCTAssertEqual(TouchRingCalibration(sectors: 20, rateHz: 50).sectors, 8)
        XCTAssertEqual(TouchRingCalibration(sectors: 0, rateHz: 50).sectors, 1)
    }
}

final class PlatterDirectionCalibrationTests: XCTestCase {
    private func spin(_ cal: inout PlatterDirectionCalibration, from: Int32, by total: Int, steps: Int = 20) {
        for i in 0...steps { cal.feed(Telemetry(jogTotal: from &+ Int32(total * i / steps))) }
    }

    func testClockwiseSpinRaisingJogTotalKeepsInvertOff() throws {
        var cal = PlatterDirectionCalibration(currentInvert: false)
        XCTAssertEqual(cal.phase, .waitingForSpin)
        spin(&cal, from: 1000, by: 400)
        let r = try cal.finish()
        XCTAssertEqual(r.invert, false)
        XCTAssertGreaterThanOrEqual(r.netTicks, 128)
        XCTAssertEqual(r.configUpdates, [ConfigUpdate(key: .jogInvert, value: 0)])
    }

    func testClockwiseSpinLoweringJogTotalSetsInvert() throws {
        var cal = PlatterDirectionCalibration(currentInvert: false)
        spin(&cal, from: 0, by: -400)
        let r = try cal.finish()
        XCTAssertTrue(r.invert)
        XCTAssertLessThanOrEqual(r.netTicks, -128)
        XCTAssertEqual(r.configUpdates, [ConfigUpdate(key: .jogInvert, value: 1)])
    }

    func testJogTotalAlreadyIncludesTheCurrentSetting() throws {
        // invert is already on and clockwise now counts up: correct, so it stays on.
        var good = PlatterDirectionCalibration(currentInvert: true)
        spin(&good, from: 0, by: 300)
        XCTAssertTrue(try good.finish().invert)

        // invert is on and clockwise counts down: the setting is wrong, so it must go off.
        var wrong = PlatterDirectionCalibration(currentInvert: true)
        spin(&wrong, from: 0, by: -300)
        XCTAssertFalse(try wrong.finish().invert)
    }

    func testSmallJitterOrTinyTurnIsNotEnough() {
        var cal = PlatterDirectionCalibration(currentInvert: false)
        spin(&cal, from: 0, by: 100)
        XCTAssertEqual(cal.phase, .waitingForSpin)
        XCTAssertThrowsError(try cal.finish()) { XCTAssertEqual($0 as? CalibrationError, .insufficientMotion(ticks: 100)) }
        XCTAssertThrowsError(try PlatterDirectionCalibration(currentInvert: false).finish())
    }

    func testDecisionIsFinalOnceMade() throws {
        var cal = PlatterDirectionCalibration(currentInvert: false)
        spin(&cal, from: 0, by: 300)
        let decided = try cal.finish()
        spin(&cal, from: 300, by: -2000)  // user spins back the other way afterwards
        XCTAssertEqual(try cal.finish(), decided)
    }

    func testBaselineIsTheFirstSampleNotZero() throws {
        var cal = PlatterDirectionCalibration(currentInvert: false)
        spin(&cal, from: 5_000_000, by: 300)
        XCTAssertFalse(try cal.finish().invert)
    }

    func testInt32RolloverMidSpinIsHandled() throws {
        var cal = PlatterDirectionCalibration(currentInvert: false)
        spin(&cal, from: Int32.max - 100, by: 300)  // crosses Int32.max
        let r = try cal.finish()
        XCTAssertFalse(r.invert)
        XCTAssertGreaterThan(r.netTicks, 0)
    }
}

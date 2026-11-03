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

// MARK: - AS5600 flow: hold pad, encoder check, step selection

final class HoldPadCalibrationTests: XCTestCase {
    private let rate = 50
    private let idleMean = 20_000.0

    /// The pad `rise` per-mille over idle (nil = no hand) with p-p jitter. The other seven pads carry `others`:
    /// a one-pad build reports 0 there, and the wizard must not care what they hold.
    private func frame(rise: Double?, jitter: Double = 0, step: Int = 0, others: UInt16 = 0) -> Telemetry {
        var raw = [UInt16](repeating: others, count: 8)
        let wobble = (step % 2 == 0 ? 0.5 : -0.5) * jitter
        raw[0] = UInt16((idleMean * (1 + ((rise ?? 0) + wobble) / 1000)).rounded())
        return Telemetry(touchRaw: raw)
    }

    private func run(_ cal: inout HoldPadCalibration, rise: Double?, samples: Int, jitter: Double = 0, others: UInt16 = 0) {
        for i in 0..<samples { cal.feed(frame(rise: rise, jitter: jitter, step: i, others: others)) }
    }

    /// Idle for 1 s, a hand arriving, held for 1 s, lifted.
    private func fullRun(_ cal: inout HoldPadCalibration, handRise: Double, jitter: Double = 0, others: UInt16 = 0) {
        run(&cal, rise: nil, samples: rate, jitter: jitter, others: others)
        run(&cal, rise: nil, samples: 5, jitter: jitter, others: others)
        run(&cal, rise: handRise, samples: rate, jitter: jitter, others: others)
        run(&cal, rise: nil, samples: 6, jitter: jitter, others: others)
    }

    func testThresholdsAreFiftyAndSixtyPercentOfTheHandsRise() throws {
        var cal = HoldPadCalibration(rateHz: rate)
        fullRun(&cal, handRise: 40, jitter: 2)
        XCTAssertEqual(cal.phase, .done)

        let r = try cal.finish()
        XCTAssertEqual(r.onPermille, 20)   // 50% of 40
        XCTAssertEqual(r.offPermille, 12)  // 60% of 20
        XCTAssertEqual(r.noisePermille, 2, accuracy: 0.05)
        XCTAssertEqual(r.risePermille, 39, accuracy: 0.1, "the lower of two consecutive samples, so half the jitter under the true rise")
        XCTAssertEqual(r.idleMean, idleMean, accuracy: 1)
        XCTAssertEqual(r.configUpdates, [ConfigUpdate(key: .touchOnPermille, value: 20),
                                         ConfigUpdate(key: .touchOffPermille, value: 12)])
    }

    func testRatiosReproduceTheShippedDefaults() throws {
        var cal = HoldPadCalibration(rateHz: rate)
        fullRun(&cal, handRise: 24)
        let r = try cal.finish()
        XCTAssertEqual(r.onPermille, 12)
        XCTAssertEqual(r.offPermille, 7)
    }

    func testNoiseFloorRaisesOffAndKeepsOnAboveIt() throws {
        var cal = HoldPadCalibration(rateHz: rate)
        fullRun(&cal, handRise: 24, jitter: 5)   // 3 x 5 per-mille noise = 15; 50% / 60% alone would give 11 / 7
        let r = try cal.finish()
        XCTAssertEqual(r.noisePermille, 5, accuracy: 0.05)
        XCTAssertEqual(r.offPermille, 15, "never below 3x idle noise")
        XCTAssertEqual(r.onPermille, 16, "on stays above off")
    }

    /// The hold pad is the ring wizard with one sector: the same stream must give the same numbers.
    func testGivesTheRingWizardsNumbersForTheSameStream() throws {
        for (handRise, jitter) in [(40.0, 2.0), (24.0, 0.0), (24.0, 5.0), (90.0, 1.0)] {
            var pad = HoldPadCalibration(rateHz: rate)
            var ring = TouchRingCalibration(sectors: 1, rateHz: rate)
            var step = 0
            func both(_ rise: Double?, _ n: Int) {
                for _ in 0..<n {
                    let t = frame(rise: rise, jitter: jitter, step: step)
                    pad.feed(t)
                    ring.feed(t)
                    step += 1
                }
            }
            both(nil, rate); both(nil, 5); both(handRise, rate); both(nil, 6)
            let a = try pad.finish(), b = try ring.finish()
            XCTAssertEqual([a.onPermille, a.offPermille], [b.onPermille, b.offPermille], "rise \(handRise) jitter \(jitter)")
            XCTAssertEqual(a.risePermille, b.risePermille[0])
            XCTAssertEqual(a.noisePermille, b.noisePermille)
        }
    }

    func testOnlyPadZeroIsRead() throws {
        // Whatever the other seven pads hold, even a huge value that appears only after the idle capture, is ignored.
        var cal = HoldPadCalibration(rateHz: rate)
        run(&cal, rise: nil, samples: rate, others: 5)
        XCTAssertEqual(cal.phase, .awaitingHand)
        run(&cal, rise: nil, samples: 30, others: 60_000)
        XCTAssertEqual(cal.phase, .awaitingHand, "a hand on pads 1...7 is not a hand on the hold pad")

        var clean = HoldPadCalibration(rateHz: rate)
        var noisy = HoldPadCalibration(rateHz: rate)
        fullRun(&clean, handRise: 40, jitter: 2, others: 0)
        fullRun(&noisy, handRise: 40, jitter: 2, others: 12_345)
        XCTAssertEqual(try clean.finish(), try noisy.finish())
    }

    func testPhasesWalkTheWizard() {
        var cal = HoldPadCalibration(rateHz: rate)
        XCTAssertEqual(cal.phase, .capturingIdle(collected: 0, needed: 50))
        run(&cal, rise: nil, samples: 10)
        XCTAssertEqual(cal.phase, .capturingIdle(collected: 10, needed: 50))
        run(&cal, rise: nil, samples: 40)
        XCTAssertEqual(cal.phase, .awaitingHand)
        XCTAssertThrowsError(try cal.finish()) { XCTAssertEqual($0 as? CalibrationError, .notComplete) }

        run(&cal, rise: 40, samples: 1)
        guard case .holding = cal.phase else { return XCTFail("\(cal.phase)") }
        run(&cal, rise: 40, samples: 30)
        XCTAssertEqual(cal.phase, .awaitingRelease)
        run(&cal, rise: 40, samples: 10)
        XCTAssertEqual(cal.phase, .awaitingRelease, "hand still down")
        run(&cal, rise: nil, samples: 3)
        XCTAssertEqual(cal.phase, .done)
    }

    func testOneSampleSpikeCannotSetThePeak() throws {
        var cal = HoldPadCalibration(rateHz: rate)
        run(&cal, rise: nil, samples: rate)
        run(&cal, rise: 40, samples: 20)
        run(&cal, rise: 400, samples: 1)
        run(&cal, rise: 40, samples: 10)
        run(&cal, rise: nil, samples: 5)
        let r = try cal.finish()
        XCTAssertEqual(r.risePermille, 40, accuracy: 0.1)
        XCTAssertEqual(r.onPermille, 20)
    }

    // MARK: failures

    func testHandOnThePlatterDuringIdleFailsAsUnstable() {
        var cal = HoldPadCalibration(rateHz: rate)
        run(&cal, rise: nil, samples: 49)
        run(&cal, rise: 60, samples: 1)
        guard case .failed(.unstableIdle) = cal.phase else { return XCTFail("\(cal.phase)") }
        XCTAssertThrowsError(try cal.finish())
    }

    func testDisconnectedPadIsNamedAsThePadNotASector() {
        var cal = HoldPadCalibration(rateHz: rate)
        for _ in 0..<50 { cal.feed(Telemetry(touchRaw: [0, 20000, 20000, 0, 0, 0, 0, 0])) }
        XCTAssertEqual(cal.phase, .failed(.padNotConnected))
        XCTAssertThrowsError(try cal.finish()) { XCTAssertEqual($0 as? CalibrationError, .padNotConnected) }
    }

    func testNoHandWithinTheTimeoutIsNamedAsNoHand() {
        var cal = HoldPadCalibration(rateHz: rate, handTimeoutSeconds: 2)
        run(&cal, rise: nil, samples: 50)
        run(&cal, rise: nil, samples: 99)
        XCTAssertEqual(cal.phase, .awaitingHand)
        run(&cal, rise: nil, samples: 1)
        XCTAssertEqual(cal.phase, .failed(.noHandDetected))
        XCTAssertThrowsError(try cal.finish()) { XCTAssertEqual($0 as? CalibrationError, .noHandDetected) }
    }

    func testAHandTooWeakForTheNoiseFailsForContrast() {
        var cal = HoldPadCalibration(rateHz: rate)
        fullRun(&cal, handRise: 14, jitter: 5)   // reads 11.5 at its low phase; 3x noise needs on >= 16
        XCTAssertEqual(cal.phase, .done)
        XCTAssertThrowsError(try cal.finish()) {
            guard case let CalibrationError.insufficientContrast(weakest, on) = $0 else { return XCTFail("\($0)") }
            XCTAssertEqual(weakest, 11)
            XCTAssertEqual(on, 16)
        }
    }

    func testFeedingAfterCompletionChangesNothing() throws {
        var cal = HoldPadCalibration(rateHz: rate)
        fullRun(&cal, handRise: 40)
        let before = try cal.finish()
        run(&cal, rise: 500, samples: 100)
        XCTAssertEqual(try cal.finish(), before)
    }
}

final class EncoderDiagnosisTests: XCTestCase {
    private let rate = 50
    private let caps: Caps = [.as5600, .dac, .link]

    private func sample(_ angle: Int, valid: Bool = true) -> Telemetry {
        Telemetry(flags: valid ? StateFlags.angleValid.rawValue : 0, angle: UInt16(((angle % 4096) + 4096) % 4096))
    }

    /// A smooth spin: `turns` turns (negative = the other way) in `steps` samples from angle `from`.
    private func spin(_ d: inout EncoderDiagnosis, turns: Double, steps: Int, from: Int = 0, jitter: Int = 0) {
        for i in 0...steps {
            let angle = from + Int((turns * 4096 * Double(i) / Double(steps)).rounded()) + (i % 2 == 0 ? jitter : -jitter)
            d.feed(sample(angle))
        }
    }

    private func waitOut(_ d: inout EncoderDiagnosis, with t: Telemetry, seconds: Double = 10) {
        for _ in 0..<Int(seconds * Double(rate)) { d.feed(t) }
    }

    func testASmoothSpinIsOkAndReportsTheTurnsMeasured() {
        var d = EncoderDiagnosis(caps: caps, rateHz: rate)
        XCTAssertEqual(d.phase, .measuring(spanCounts: 0, requiredCounts: 4096))
        spin(&d, turns: 1.5, steps: 75, from: 1000)   // wraps through 4095 -> 0 on the way
        guard case let .ok(turns, invalid)? = d.verdict else { return XCTFail("\(d.phase)") }
        XCTAssertEqual(turns, 1.0, accuracy: 0.05, "decided as soon as one full turn is covered")
        XCTAssertEqual(invalid, 0)
        XCTAssertTrue(d.verdict?.isOK ?? false)
    }

    func testTheTravelIsRightWhenTheAngleWrapsInEitherDirection() {
        for (from, turns) in [(3000, 0.5), (1000, -0.5)] {   // 4095 -> 0 going up, 0 -> 4095 going down
            var d = EncoderDiagnosis(caps: caps, rateHz: rate)
            spin(&d, turns: turns, steps: 40, from: from)
            guard case let .measuring(span, _) = d.phase else { return XCTFail("\(d.phase)") }
            XCTAssertEqual(span, 2048, accuracy: 4, "from \(from), \(turns) turn")
        }
    }

    func testAFullTurnInAFewSamplesIsNotBelievedUntilTenAreIn() {
        // At 20 Hz a step may be up to 1024 counts, so a full turn can arrive in 5 samples: too few to trust.
        var d = EncoderDiagnosis(caps: caps, rateHz: 20)
        for angle in [0, 1000, 2000, 3000, 4000, 5000] { d.feed(sample(angle)) }
        XCTAssertNil(d.verdict, "span \(d.spanCounts) after 5 steps")
        for _ in 0..<5 { d.feed(sample(5000)) }
        XCTAssertTrue(d.verdict?.isOK ?? false, "\(d.phase)")
    }

    func testTheOtherDirectionIsJustAsGood() {
        var d = EncoderDiagnosis(caps: caps, rateHz: rate)
        spin(&d, turns: -1.5, steps: 75, from: 300)
        XCTAssertTrue(d.verdict?.isOK ?? false, "\(d.phase)")
    }

    func testBackAndForthOverAFullTurnCounts() {
        var d = EncoderDiagnosis(caps: caps, rateHz: rate)
        spin(&d, turns: 0.6, steps: 30)
        spin(&d, turns: -1.3, steps: 60, from: 2458)
        XCTAssertTrue(d.verdict?.isOK ?? false, "\(d.phase)")
    }

    func testProgressGrowsWithTheTravel() {
        var d = EncoderDiagnosis(caps: caps, rateHz: rate)
        spin(&d, turns: 0.25, steps: 20)
        guard case let .measuring(span, required) = d.phase else { return XCTFail("\(d.phase)") }
        XCTAssertEqual(span, 1024, accuracy: 4)
        XCTAssertEqual(required, 4096)
        XCTAssertNil(d.verdict)
    }

    func testInvalidReadsAlongTheWayAreCountedNotFatal() {
        var d = EncoderDiagnosis(caps: caps, rateHz: rate)
        for i in 0...75 {
            d.feed(sample(1000 + i * 60, valid: i % 10 != 5))   // every tenth read fails
        }
        guard case let .ok(_, invalid)? = d.verdict else { return XCTFail("\(d.phase)") }
        XCTAssertEqual(invalid, 7, "reads 5, 15, ... 65 failed before the turn was complete at read 69")
    }

    // MARK: failures, one verdict each

    func testNoChipAtStartUpIsDecidedImmediatelyWhateverTheTelemetryLooksLike() {
        var d = EncoderDiagnosis(caps: [.touchRing, .dac], rateHz: rate)
        XCTAssertEqual(d.phase, .decided(.noEncoder))
        spin(&d, turns: 2, steps: 80)
        XCTAssertEqual(d.verdict, .noEncoder, "the unit's own word about the chip is not overridden by a stray reading")
    }

    func testAChipThatAnsweredButNeverGivesAValidAngle() {
        var d = EncoderDiagnosis(caps: caps, rateHz: rate)
        waitOut(&d, with: sample(0, valid: false), seconds: 9.5)
        XCTAssertNil(d.verdict, "not before the time is up")
        waitOut(&d, with: sample(0, valid: false), seconds: 0.5)
        XCTAssertEqual(d.verdict, .readsFailing(samples: 500))
    }

    func testAStillPlatterIsNotMovingAndSaysHowLittleItMoved() {
        var d = EncoderDiagnosis(caps: caps, rateHz: rate)
        for i in 0..<500 { d.feed(sample(2000 + (i % 3) - 1)) }   // +-1 count of jitter, no spin
        guard case let .notMoving(span)? = d.verdict else { return XCTFail("\(d.phase)") }
        XCTAssertLessThanOrEqual(span, 3)
    }

    func testHalfATurnIsNotEnough() {
        var d = EncoderDiagnosis(caps: caps, rateHz: rate)
        spin(&d, turns: 0.5, steps: 40)
        waitOut(&d, with: sample(2048))
        guard case let .notMoving(span)? = d.verdict else { return XCTFail("\(d.phase)") }
        XCTAssertEqual(span, 2048, accuracy: 4)
    }

    func testAngleThatJumpsAroundIsErraticNotAnOk() {
        var d = EncoderDiagnosis(caps: caps, rateHz: rate)
        var x: UInt32 = 12345   // deterministic noise over the whole 12-bit range
        for _ in 0..<500 {
            x = x &* 1_664_525 &+ 1_013_904_223
            d.feed(sample(Int(x >> 16) & 4095))
        }
        guard case let .erratic(jumps, steps)? = d.verdict else { return XCTFail("\(d.phase)") }
        XCTAssertGreaterThan(jumps * 4, steps)
    }

    func testAnOddGlitchInAStillSignalNeitherAddsTravelNorMakesItOk() {
        var d = EncoderDiagnosis(caps: caps, rateHz: rate)
        for i in 0..<500 {
            // Every 20th sample reads 1800 counts away, always on the same side: if glitches counted as travel
            // the span would pass a full turn after three of them.
            d.feed(sample(i % 20 == 10 ? 3800 : 2000))
        }
        guard case let .notMoving(span)? = d.verdict else { return XCTFail("\(d.phase)") }
        XCTAssertEqual(span, 0, "the jumps are glitches, not motion")
    }

    func testTheVerdictIsFinal() {
        var d = EncoderDiagnosis(caps: caps, rateHz: rate)
        spin(&d, turns: 1.5, steps: 75)
        let decided = d.verdict
        XCTAssertNotNil(decided)
        waitOut(&d, with: sample(0, valid: false))
        XCTAssertEqual(d.verdict, decided)
    }

    func testTheStepLimitFollowsTheStreamRate() {
        // 300 counts per sample is 3.7 turns/s at 50 Hz (a hard flick) but 7.3 turns/s at 100 Hz: not a hand.
        var slow = EncoderDiagnosis(caps: caps, rateHz: 50)
        var fast = EncoderDiagnosis(caps: caps, rateHz: 100)
        for i in 0..<40 {
            slow.feed(sample(i * 300))
            fast.feed(sample(i * 300))
        }
        XCTAssertTrue(slow.verdict?.isOK ?? false, "\(slow.phase)")
        XCTAssertNil(fast.verdict)
        for i in 40..<1000 { fast.feed(sample(i * 300)) }
        guard case .erratic? = fast.verdict else { return XCTFail("\(fast.phase)") }
    }
}

final class CalibrationFlowTests: XCTestCase {
    func testEachPlatterSourceGetsItsOwnOrder() {
        XCTAssertEqual(CalibrationFlow.steps(for: .as5600), [.encoder, .direction, .holdPad, .fader, .knob])
        XCTAssertEqual(CalibrationFlow.steps(for: .touchRing), [.fader, .knob, .touchRing, .direction], "the ring flow is unchanged")
        XCTAssertEqual(CalibrationFlow.steps(for: .touchHoldOnly), [.holdPad, .fader, .knob])
    }

    func testNoFlowRepeatsAStepAndEveryStepIsInSomeFlow() {
        var seen = Set<CalibrationStep>()
        for source in PlatterSource.allCases {
            let steps = CalibrationFlow.steps(for: source)
            XCTAssertEqual(Set(steps).count, steps.count, "\(source)")
            seen.formUnion(steps)
        }
        XCTAssertEqual(seen, Set(CalibrationStep.allCases))
    }

    func testEncoderAndHoldPadAreNotOfferedWithTheWrongHardware() {
        XCTAssertFalse(CalibrationFlow.steps(for: .touchRing).contains(.encoder))
        XCTAssertFalse(CalibrationFlow.steps(for: .touchRing).contains(.holdPad))
        XCTAssertFalse(CalibrationFlow.steps(for: .touchHoldOnly).contains(.encoder))
        XCTAssertFalse(CalibrationFlow.steps(for: .touchHoldOnly).contains(.direction), "no rotation is reported")
        XCTAssertFalse(CalibrationFlow.steps(for: .as5600).contains(.touchRing))
    }

    func testTheSettingBeatsInfoAndTheDefaultIsAS5600() {
        XCTAssertEqual(CalibrationFlow.source(config: 1, info: 0), .touchRing, "the live setting wins")
        XCTAssertEqual(CalibrationFlow.source(config: nil, info: 2), .touchHoldOnly, "before the settings are read, Info")
        XCTAssertEqual(CalibrationFlow.source(config: nil, info: nil), .as5600)
        XCTAssertEqual(CalibrationFlow.source(config: 9, info: 1), .touchRing, "an unknown setting falls through to Info")
        XCTAssertEqual(CalibrationFlow.source(config: -1, info: 9), .as5600, "nothing usable: the factory default")
        XCTAssertEqual(PlatterSource(rawValue: UInt8(ConfigTable.desc(.platterSource).defaultValue)), .as5600,
                       "the factory default this flow assumes is the contract's")
    }
}

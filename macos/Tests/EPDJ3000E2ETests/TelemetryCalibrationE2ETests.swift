// Telemetry streaming and the calibration recipes of PROTOCOL.md 3.2, with the real DeviceClient and the real
// firmware: the host-side wizards (Calibration.swift) are fed by the unit's own telemetry while the simulated
// hardware does what a person would, and the thresholds they produce are then put to the test on the ring.

import EPDJ3000Protocol
import XCTest

final class TelemetryCalibrationE2ETests: E2ETestCase {
    // MARK: telemetry stream

    func testTelemetryCarriesTheSensorsAndTheJogTotalExactly() async throws {
        try await withWorld { world in
            let c = world.client()
            _ = try await world.command("fader u=0 raw=3000 noise=0")
            _ = try await world.command("knob u=0 raw=1234 noise=0")
            try await c.client.startStream(rateHz: 100)
            try await world.run(ms: 200)

            var frames = c.telemetry.take()
            XCTAssertGreaterThanOrEqual(frames.count, 18)
            var t = try XCTUnwrap(frames.last)
            XCTAssertEqual(t.faderRaw, 3000)
            XCTAssertEqual(t.knobRaw, 1234)
            // 14-bit outputs per PROTOCOL.md 2.4: default calibration 40...4055, 1.5 % dead zones, linear
            let fader = Double(3000 - 40) / Double(4055 - 40)
            let expectedFader = (fader - 0.015) / 0.97 * 16383
            XCTAssertEqual(Double(t.faderOut), expectedFader, accuracy: 30)
            XCTAssertEqual(Double(t.knobOut), 1234.0 / 4095.0 * 16383, accuracy: 2, "knob has no dead zone and spans 0...4095")
            XCTAssertEqual(StateFlags(rawValue: t.flags).contains(.usbMounted), true)
            XCTAssertEqual(StateFlags(rawValue: t.flags).contains(.touched), false)
            XCTAssertEqual(StateFlags(rawValue: t.flags).contains(.linkUp), false)
            XCTAssertEqual(t.touchRaw.count, 8)
            XCTAssertEqual(Double(t.touchRaw[0]), 30000, accuracy: 30000 * 0.004, "idle reading of the hold pad (GPIO4, the default build's one pad)")
            XCTAssertEqual(Array(t.touchRaw.dropFirst()), [UInt16](repeating: 0, count: 7), "pads that are not wired read 0")
            XCTAssertTrue(StateFlags(rawValue: t.flags).contains(.angleValid), "the encoder answers")
            XCTAssertEqual(t.angle, 0)
            XCTAssertEqual(t.bpmX10, 1200)
            XCTAssertEqual(t.jogTotal, 0)

            // frames are consecutive
            for (a, b) in zip(frames, frames.dropFirst()) { XCTAssertEqual(b.seq, a.seq &+ 1) }

            // Buttons
            _ = try await world.command("buttons u=0 cue=1")
            try await world.run(ms: 50)
            t = try XCTUnwrap(c.telemetry.take().last)
            XCTAssertEqual(Buttons(rawValue: t.buttons), [.cue])
            _ = try await world.command("buttons u=0 rev=1")
            try await world.run(ms: 50)
            t = try XCTUnwrap(c.telemetry.take().last)
            XCTAssertEqual(Buttons(rawValue: t.buttons), [.cue, .rev])
            _ = try await world.command("buttons u=0 cue=0 rev=0")
            try await world.run(ms: 50)
            _ = c.telemetry.take()

            // jogTotal is an i32 on the wire and cumulative after jogInvert: negative for a counter-clockwise turn
            try await world.gesture(revs: -1.5, ms: 3000)
            frames = c.telemetry.take()
            t = try XCTUnwrap(frames.last)
            let sim = try await world.status()
            XCTAssertEqual(Int(t.jogTotal), sim.jogTotal, "telemetry jogTotal is what the firmware counted")
            XCTAssertEqual(t.jogTotal, -1536, "1.5 revolutions of the encoder, counter-clockwise: exactly 1536 ticks")
            XCTAssertEqual(t.angle, 2048, "Telemetry.angle is the encoder's 12-bit reading: -1.5 rev is half a turn")
            XCTAssertTrue(frames.contains { StateFlags(rawValue: $0.flags).contains(.touched) }, "the touched flag was seen")
            XCTAssertTrue(frames.contains { StateFlags(rawValue: $0.flags).contains(.angleValid) }, "the angle was valid while moving")
            try await c.client.stopStream()
        }
    }

    func testStreamRateIsClampedAndTheClientKeepAliveOutlastsTheDevicesFiveSecondTimeout() async throws {
        try await withWorld { world in
            let c = world.client()
            try await c.client.startStream(rateHz: 1000)  // the client clamps to 100
            try await world.run(ms: 1000)
            let first = c.telemetry.take().count
            XCTAssertEqual(Double(first), 100, accuracy: 3)

            try await world.run(ms: 12000)  // virtual: well past 5 s, so only the keep-alive can have kept it going
            let long = c.telemetry.take().count
            XCTAssertEqual(Double(long), 1200, accuracy: 30, "still streaming after 12 s")

            try await c.client.stopStream()
            _ = c.telemetry.take()
            try await world.run(ms: 1000)
            XCTAssertEqual(c.telemetry.take().count, 0, "STREAM 0 stops it")
        }
    }

    func testWithoutARefreshTheDeviceStopsStreamingAfterFiveSeconds() async throws {
        try await withWorld { world in
            let c = world.client(streamRefreshInterval: 10_000)  // effectively never refreshes
            try await c.client.startStream(rateHz: 20)
            try await world.run(ms: 8000)
            let frames = c.telemetry.take()
            XCTAssertEqual(Double(frames.count), 100, accuracy: 4, "20 Hz for the 5 s the device keeps streaming on its own")
        }
    }

    // MARK: fader / knob auto calibration

    /// A fader whose pot only travels 300...3800 of the ADC's range, swept end to end from the user's side.
    private func sweepElectricalRange(_ world: SimWorld, from lo: Double = 300, to hi: Double = 3800) async throws {
        _ = try await world.command("fader u=0 raw=\(Int((lo + hi) / 2))")
        try await world.run(ms: 100)
        try await world.ramp("fader", from: (lo + hi) / 2, to: lo, ms: 300)
        try await world.run(ms: 100)
        try await world.ramp("fader", from: lo, to: hi, ms: 800)
        try await world.run(ms: 100)
    }

    /// Min and max of the 14-bit fader values the unit sent for a full there-and-back sweep of the pot.
    private func outputRange(_ world: SimWorld, from lo: Double, to hi: Double) async throws -> (min: Int, max: Int) {
        let mark = world.process.log.count
        try await world.ramp("fader", from: hi, to: lo, ms: 600)
        try await world.run(ms: 100)
        try await world.ramp("fader", from: lo, to: hi, ms: 600)
        try await world.run(ms: 200)
        let values = world.faderValues(since: mark)
        return (values.min() ?? -1, values.max() ?? -1)
    }

    func testFaderCalibrationOnTheDeviceMakesTheWholeTravelReachTheWholeOutputRange() async throws {
        try await withWorld { world in
            let c = world.client()
            // Before: the default 40...4055 calibration does not fit this pot.
            _ = try await world.command("fader u=0 raw=2048")
            try await world.run(ms: 200)
            let before = try await outputRange(world, from: 300, to: 3800)
            XCTAssertGreaterThan(before.min, 100, "the low end of the travel does not reach 0")
            XCTAssertLessThan(before.max, 16000, "the high end does not reach 16383")

            try await c.client.calAutoBegin(.fader)
            try await sweepElectricalRange(world)
            let result = try await c.client.calAutoEnd(.fader, apply: true)
            XCTAssertEqual(result.kind, CalKind.fader.rawValue)
            XCTAssertEqual(result.applied, 1)
            // The device keeps min/max of a 3-sample median of the 1 kHz readings and insets them by 1 % of the span.
            let inset = 35.0
            XCTAssertEqual(Double(result.rawMin), 300 + inset, accuracy: 5)
            XCTAssertEqual(Double(result.rawMax), 3800 - inset, accuracy: 5)
            try await expectEqual(try await c.client.getConfig(.faderRawMin), Int32(result.rawMin), "CalResult carries what was written")
            try await expectEqual(try await c.client.getConfig(.faderRawMax), Int32(result.rawMax))

            let after = try await outputRange(world, from: 300, to: 3800)
            XCTAssertEqual(after.min, 0, "bottom of the travel is 0 after calibration")
            XCTAssertEqual(after.max, 16383, "top of the travel is 16383 after calibration")
        }
    }

    func testFaderCalibrationFailureAndNotActiveAreReportedAndChangeNothing() async throws {
        try await withWorld { world in
            let c = world.client().client
            do {
                _ = try await c.calAutoEnd(.fader, apply: true)
                XCTFail("CAL_END without CAL_BEGIN")
            } catch { XCTAssertEqual(error as? DeviceError, .nak(.calEnd, .calNotActive)) }

            try await c.calAutoBegin(.fader)
            _ = try await world.command("fader u=0 raw=2000")
            try await world.run(ms: 100)
            try await world.ramp("fader", from: 2000, to: 2900, ms: 300)  // 900 counts: less than the 1500 the device demands
            try await world.run(ms: 100)
            do {
                _ = try await c.calAutoEnd(.fader, apply: true)
                XCTFail("a 900-count sweep must fail")
            } catch { XCTAssertEqual(error as? DeviceError, .nak(.calEnd, .calFailed)) }
            try await expectEqual(try await c.getConfig(.faderRawMin), ConfigTable.desc(.faderRawMin).defaultValue)
            try await expectEqual(try await c.getConfig(.faderRawMax), ConfigTable.desc(.faderRawMax).defaultValue)
            do {
                _ = try await c.calAutoEnd(.fader, apply: true)
                XCTFail("the failed attempt ended the calibration")
            } catch { XCTAssertEqual(error as? DeviceError, .nak(.calEnd, .calNotActive)) }

            // apply = 0 reports the measured extremes and writes nothing
            try await c.calAutoBegin(.knob)
            _ = try await world.command("knob u=0 raw=100")
            try await world.run(ms: 100)
            try await world.ramp("knob", from: 100, to: 3900, ms: 500)
            try await world.run(ms: 100)
            let measured = try await c.calAutoEnd(.knob, apply: false)
            XCTAssertEqual(measured.applied, 0)
            // PROTOCOL.md 3: with apply = 0 the result carries "the measured extremes", not the 1 % inset range apply writes.
            XCTAssertEqual(Double(measured.rawMin), 100, accuracy: 4, "measured, not inset")
            XCTAssertEqual(Double(measured.rawMax), 3900, accuracy: 4)
            try await expectEqual(try await c.getConfig(.knobRawMin), ConfigTable.desc(.knobRawMin).defaultValue)
            try await expectEqual(try await c.getConfig(.knobRawMax), ConfigTable.desc(.knobRawMax).defaultValue)

            // The same sweep with apply = 1 reports, and writes, the range moved in by 1 % of the 3800-count span.
            try await c.calAutoBegin(.knob)
            _ = try await world.command("knob u=0 raw=100")
            try await world.run(ms: 100)
            try await world.ramp("knob", from: 100, to: 3900, ms: 500)
            try await world.run(ms: 100)
            let applied = try await c.calAutoEnd(.knob, apply: true)
            XCTAssertEqual(applied.applied, 1)
            XCTAssertEqual(Double(applied.rawMin), 138, accuracy: 4, "inset range")
            XCTAssertEqual(Double(applied.rawMax), 3862, accuracy: 4)
            try await expectEqual(try await c.getConfig(.knobRawMin), Int32(applied.rawMin), "CalResult carries what was written")
            try await expectEqual(try await c.getConfig(.knobRawMax), Int32(applied.rawMax))
        }
    }

    func testHostSideSweepCalibrationAgreesWithTheDevicesAndFindsAReversedPot() async throws {
        try await withWorld { world in
            let c = world.client()
            try await c.client.startStream(rateHz: 100)
            _ = try await world.command("fader u=0 raw=2048")
            try await world.run(ms: 200)
            _ = c.telemetry.take()

            try await c.client.calAutoBegin(.fader)
            var host = SweepCalibration(kind: .fader)
            // reversed pot: raw FALLS towards the "high" end, so the sweep goes from 3800 down to 300
            _ = try await world.command("fader u=0 raw=3800")
            try await world.run(ms: 200)
            try await world.ramp("fader", from: 3800, to: 300, ms: 1000)
            try await world.run(ms: 200)
            for t in c.telemetry.take() { host.feed(t) }
            XCTAssertEqual(host.phase, .rangeReached)
            let deviceResult = try await c.client.calAutoEnd(.fader, apply: true)  // apply = 1 reports the inset range, like the host
            let hostResult = try host.finish()
            XCTAssertEqual(hostResult.invert, true, "raw fell as the control moved to its high end")
            XCTAssertEqual(Double(hostResult.rawMin), 300, accuracy: 6)
            XCTAssertEqual(Double(hostResult.rawMax), 3800, accuracy: 6)
            let min = try XCTUnwrap(hostResult.configUpdates.first { $0.key == .faderRawMin }).value
            let max = try XCTUnwrap(hostResult.configUpdates.first { $0.key == .faderRawMax }).value
            XCTAssertEqual(Double(min), Double(deviceResult.rawMin), accuracy: 6, "host and device insets agree")
            XCTAssertEqual(Double(max), Double(deviceResult.rawMax), accuracy: 6)

            // Apply the host's answer and check the control now works in the right direction.
            try await c.client.apply(hostResult.configUpdates)
            let mark = world.process.log.count
            try await world.ramp("fader", from: 300, to: 3800, ms: 500)  // towards the control's LOW end
            try await world.run(ms: 200)
            XCTAssertEqual(try XCTUnwrap(world.faderValues(since: mark).last), 0, "the low end of the control outputs 0")
            try await world.ramp("fader", from: 3800, to: 300, ms: 500)  // and back to the HIGH end
            try await world.run(ms: 200)
            XCTAssertEqual(try XCTUnwrap(world.faderValues(since: mark).last), 16383, "the high end outputs 16383")
        }
    }

    // MARK: touch ring wizard

    /// Drives the wizard the way a person would: waits for each prompt, puts a finger on the asked sector, holds, lifts.
    private func runTouchRingWizard(_ world: SimWorld, _ c: SimClient, sectors: Int = 8) async throws -> TouchRingCalibration {
        var wizard = TouchRingCalibration(sectors: sectors, rateHz: 100)
        var onSector: Int?
        for step in 0..<4000 {
            try await world.run(ms: 10)
            for t in c.telemetry.take() { wizard.feed(t) }
            switch wizard.phase {
            case let .awaitingFinger(s):
                if onSector != s {
                    try await world.finger(rev: Double(s) / Double(sectors), pressure: 1)
                    onSector = s
                }
            case .awaitingRelease:
                if onSector != nil {
                    try await world.finger(pressure: 0)
                    onSector = nil
                }
            case .done, .failed:
                try await world.finger(pressure: 0)
                return wizard
            default:
                break
            }
            XCTAssertLessThan(step, 3999, "the wizard never finished: \(wizard.phase)")
        }
        return wizard
    }

    /// Lands a finger at `rev`, waits, reports whether the unit said "touched" and how soon, then lifts and reports the same.
    private func touchResponse(_ world: SimWorld, rev: Double) async throws -> (downMs: Double?, upMs: Double?) {
        let mark = world.process.log.count
        let t0 = world.virtualSeconds
        try await world.finger(rev: rev, pressure: 1)
        try await world.run(ms: 100)
        let tLift = world.virtualSeconds
        try await world.finger(pressure: 0)
        try await world.run(ms: 100)
        var down: Double?, up: Double?
        for (t, ev) in world.midiEvents(from: mark) {
            switch ev {
            case let .noteOn(_, note, _) where note == Proto.Note.touch && down == nil: down = (Double(t) / 1e6 - t0) * 1000
            case let .noteOff(_, note, _) where note == Proto.Note.touch && up == nil: up = (Double(t) / 1e6 - tLift) * 1000
            default: break
            }
        }
        return (down, up)
    }

    func testTouchRingWizardProducesThresholdsThatDetectTheFingerAnywhereOnTheRing() async throws {
        try await withWorld("units=1 platter=ring") { world in  // the experimental ring: eight pads, selected explicitly
            let c = world.client()
            try await c.client.startStream(rateHz: 100)
            let wizard = try await runTouchRingWizard(world, c)
            guard case .done = wizard.phase else { return XCTFail("wizard ended in \(wizard.phase)") }
            let result = try wizard.finish()

            XCTAssertEqual(result.sectors, 8)
            for (i, rise) in result.risePermille.enumerated() {
                XCTAssertEqual(rise, 40, accuracy: 6, "sector \(i): the simulated finger raises its sector by 40 per mille")
            }
            XCTAssertEqual(result.noisePermille, 6, accuracy: 1.5, "+-3 per mille uniform noise is 6 peak to peak")
            XCTAssertGreaterThan(result.onPermille, result.offPermille, "hysteresis survives the noise floor")
            XCTAssertGreaterThanOrEqual(Double(result.offPermille), 3 * result.noisePermille - 0.01, "off is never below 3x the idle noise")
            XCTAssertLessThan(Double(result.onPermille), (result.risePermille.min() ?? 0) * 0.75,
                              "on must leave room for a finger between two sectors (about 75 % of a centred one)")

            try await c.client.apply(result.configUpdates)
            try await c.client.saveConfig()
            try await expectEqual(try await c.client.getConfig(.touchOnPermille), Int32(result.onPermille))
            try await expectEqual(try await c.client.getConfig(.touchOffPermille), Int32(result.offPermille))
            try await c.client.stopStream()

            // Does the ring now detect the finger? Sixteen positions: on each sector and in the gaps between them.
            var worstDown = 0.0, worstUp = 0.0
            for i in 0..<16 {
                let r = try await touchResponse(world, rev: Double(i) / 16)
                let down = try XCTUnwrap(r.downMs, "finger at \(i)/16 rev was not detected with the wizard's thresholds")
                let up = try XCTUnwrap(r.upMs, "lift at \(i)/16 rev was not detected")
                worstDown = max(worstDown, down)
                worstUp = max(worstUp, up)
            }
            observe("touch ring wizard: on=\(result.onPermille) off=\(result.offPermille) per mille from rises \(result.risePermille.map { Int($0.rounded()) }) "
                + "and idle noise \(String(format: "%.1f", result.noisePermille)); worst touch-down \(String(format: "%.1f", worstDown)) ms, touch-up \(String(format: "%.1f", worstUp)) ms")
            XCTAssertLessThan(worstDown, 12, "touch-down latency in ms")
            XCTAssertLessThan(worstUp, 40, "touch-up latency in ms")

            // ...and does it stay quiet when nobody touches it?
            let mark = world.process.log.count
            try await world.run(ms: 6000)
            let spurious = world.midiEvents(from: mark).filter {
                switch $0.event {
                case let .noteOn(_, n, _), let .noteOff(_, n, _): return n == Proto.Note.touch
                default: return false
                }
            }
            XCTAssertEqual(spurious.count, 0, "idle noise must not read as a touch with on=\(result.onPermille)/off=\(result.offPermille)")
        }
    }

    func testTouchRingWizardRefusesAFingerThatIsTooLightForTheNoiseFloor() async throws {
        try await withWorld("units=1 platter=ring") { world in
            let c = world.client()
            _ = try await world.command("ring u=0 peak=14")  // barely above the factory 12 per mille
            try await c.client.startStream(rateHz: 100)
            let wizard = try await runTouchRingWizard(world, c)
            observe("wizard with a 14 per mille finger ended in \(wizard.phase)")
            switch wizard.phase {
            case .failed(.insufficientContrast), .failed(.noFingerDetected):
                break
            case .done:
                // If it did finish, the thresholds it gives must at least work.
                let r = try wizard.finish()
                try await c.client.apply(r.configUpdates)
                let hit = try await touchResponse(world, rev: 0.0625)
                XCTAssertNotNil(hit.downMs, "the wizard accepted a finger that its own thresholds cannot detect: \(r)")
            default:
                XCTFail("unexpected wizard outcome \(wizard.phase)")
            }
        }
    }

    // MARK: platter direction wizard

    private func runDirectionWizard(_ world: SimWorld, _ c: SimClient, currentInvert: Bool, fingerRevsPerSecond: Double) async throws
        -> PlatterDirectionCalibration {
        var wizard = PlatterDirectionCalibration(currentInvert: currentInvert)
        let onRing = world.platter == .ring
        try await world.finger(rev: onRing ? 0.25 : nil, pressure: 1)  // the hold pad, or the ring
        var rev = 0.25
        for _ in 0..<400 {
            let dRev = fingerRevsPerSecond * 0.02
            if onRing {
                try await world.ramp("finger", from: rev, to: rev + dRev, ms: 20, slice: 20)
                rev += dRev
            } else {
                try await world.spin(revs: dRev, ms: 20)  // the platter turns under the hand
            }
            for t in c.telemetry.take() { wizard.feed(t) }
            if case .decided = wizard.phase { break }
        }
        try await world.finger(pressure: 0)
        try await world.run(ms: 200)
        return wizard
    }

    /// On the AS5600 platter this wizard matters most: which way the encoder counts depends on how the magnet and the
    /// breakout were mounted (the DIR pin, or jogInvert as its software twin). Run it on both platters.
    func testPlatterDirectionWizardKeepsAClockwiseUnitAndFlipsAReversedOne() async throws {
        for args in ["units=1", "units=1 platter=ring"] {
            try await withWorld(args) { world in
                let c = world.client()
                try await c.client.startStream(rateHz: 100)

                // Correct unit, the user spins clockwise: nothing to change.
                var wizard = try await runDirectionWizard(world, c, currentInvert: false, fingerRevsPerSecond: 0.5)
                var result = try wizard.finish()
                XCTAssertGreaterThan(result.netTicks, 0, args)
                XCTAssertEqual(result.invert, false, args)

                // The platter is wired backwards: what the user calls clockwise moves the sensor the other way round.
                wizard = try await runDirectionWizard(world, c, currentInvert: false, fingerRevsPerSecond: -0.5)
                result = try wizard.finish()
                XCTAssertLessThan(result.netTicks, 0, "jogTotal falls (\(args))")
                XCTAssertEqual(result.invert, true, args)
                try await c.client.apply(result.configUpdates)
                try await expectEqual(try await c.client.getConfig(.jogInvert), 1)

                // jogTotal is measured after jogInvert, so the same motion now counts upwards, and the wizard, run again
                // with the new setting, leaves it alone.
                wizard = try await runDirectionWizard(world, c, currentInvert: true, fingerRevsPerSecond: -0.5)
                result = try wizard.finish()
                XCTAssertGreaterThan(result.netTicks, 0, "after jogInvert = 1 the reversed motion is positive (\(args))")
                XCTAssertEqual(result.invert, true, "and the setting is kept (\(args))")
            }
        }
    }
}

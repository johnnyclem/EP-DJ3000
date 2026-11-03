import XCTest
@testable import EPDJ3000Protocol

/// The AS5600 calibration flow on the host side, end to end: a `DeviceClient` talking to the in-repo `FakeDevice`, a
/// simulated hand-spun platter on the telemetry stream, the wizards from the protocol library, and their results
/// applied and saved through the client. This is what the macOS app's `CalibrationController` does, minus SwiftUI.
///
/// What it does not show: how the real firmware behaves. `FakeDevice` models SysEx, not sensors, so `SimPlatter` is a
/// stand-in written from the jog tracker's documented rules (4 raw counts per jog tick, `jogInvert` negates the ticks,
/// the raw angle rises when the platter turns the way the chip was mounted). The real engine is exercised by
/// `EPDJ3000E2ETests`, which this file does not touch.
final class AS5600CalibrationFlowTests: XCTestCase {
    private let rate = 50

    // MARK: simulation

    private struct SimPlatter {
        static let idlePad = 20_000.0

        /// +1: the raw angle rises while the platter turns clockwise; -1: the chip or magnet is the other way round.
        var mounting = 1.0
        /// The unit's `jogInvert` right now. The test keeps it equal to what the wizard last applied.
        var invert = false

        private var counts = 0.0   // raw position, unwrapped, in 1/4096 turns
        private var carry = 0.0    // counts not yet whole ticks
        private var jogTotal: Int32 = 0
        private var seq: UInt8 = 0
        private var wobble = false

        init(mounting: Double = 1.0) {
            self.mounting = mounting
        }

        /// The platter turned clockwise by `turns` over `steps` frames; the hold pad reads `padRise` per-mille over idle.
        mutating func spin(turns: Double, steps: Int, padRise: Double = 0) -> [Telemetry] {
            let delta = mounting * turns * 4096 / Double(steps)
            var out = [Telemetry]()
            for _ in 0..<steps {
                counts += delta
                carry += delta
                let whole = (carry / 4).rounded(.towardZero)
                carry -= whole * 4
                jogTotal &+= Int32(invert ? -whole : whole)
                out.append(frame(padRise: padRise, jitter: 0))
            }
            return out
        }

        /// Nothing moves; the pad reads `padRise` per-mille over idle with `jitter` per-mille peak to peak.
        mutating func still(_ frames: Int, padRise: Double = 0, jitter: Double = 0) -> [Telemetry] {
            (0..<frames).map { _ in frame(padRise: padRise, jitter: jitter) }
        }

        private mutating func frame(padRise: Double, jitter: Double) -> Telemetry {
            seq &+= 1
            wobble.toggle()
            let pad = Self.idlePad * (1 + (padRise + (wobble ? 0.5 : -0.5) * jitter) / 1000)
            let raw = ((Int(counts.rounded()) % 4096) + 4096) % 4096
            return Telemetry(seq: seq, flags: StateFlags.angleValid.rawValue, angle: UInt16(raw), jogTotal: jogTotal,
                             touchRaw: [UInt16(pad.rounded()), 0, 0, 0, 0, 0, 0, 0])
        }
    }

    // MARK: helpers

    private func connect(_ dev: FakeDevice) -> DeviceClient {
        DeviceClient(transport: dev, deck: 0, clock: SystemClock(), timeout: 2)
    }

    /// Sends the frames from the fake unit and returns them as the client delivers them, so every value crosses the
    /// SysEx encoding and the decoder. In bursts of 128: a subscriber keeps only the newest 256 frames, as a live
    /// stream should, and the test is not a slow consumer.
    private func receive(_ frames: [Telemetry], from dev: FakeDevice, via client: DeviceClient) async -> [Telemetry] {
        var delivered = [Telemetry]()
        for start in stride(from: 0, to: frames.count, by: 128) {
            let burst = Array(frames[start..<min(frames.count, start + 128)])
            let stream = client.telemetry()
            for frame in burst { dev.emit(frame) }
            delivered += await collect(stream, count: burst.count)
        }
        XCTAssertEqual(delivered.count, frames.count, "every telemetry frame reached the client")
        return delivered
    }

    private func config(_ dev: FakeDevice, _ key: ConfigKey) -> Int32 {
        dev.read { $0.config[Int(key.rawValue)] }
    }

    // MARK: the flow

    func testAFactoryUnitRunsTheWholeAS5600FlowAndEverythingLandsInItsConfig() async throws {
        let dev = FakeDevice(deck: .a)
        let client = connect(dev)

        // What the app knows after its handshake.
        let info = try await client.getInfo()
        let settings = try await client.dumpConfig()
        let source = CalibrationFlow.source(config: settings[.platterSource], info: info.platterSource)
        XCTAssertEqual(source, .as5600, "a factory unit")
        let steps = CalibrationFlow.steps(for: source)
        XCTAssertEqual(steps, [.encoder, .direction, .holdPad, .fader, .knob])
        try await client.startStream(rateHz: rate)

        // The magnet is mounted so the raw angle FALLS when the platter turns clockwise: the direction step has work to do.
        var platter = SimPlatter(mounting: -1)

        for step in steps {
            switch step {
            case .encoder:
                var diagnosis = EncoderDiagnosis(caps: Caps(rawValue: info.caps), rateHz: rate)
                for t in await receive(platter.spin(turns: 1.5, steps: 75), from: dev, via: client) { diagnosis.feed(t) }
                guard case let .ok(turns, invalid)? = diagnosis.verdict else { return XCTFail("encoder: \(diagnosis.phase)") }
                XCTAssertEqual(turns, 1.0, accuracy: 0.05)
                XCTAssertEqual(invalid, 0)

            case .direction:
                var wizard = PlatterDirectionCalibration(currentInvert: (settings[.jogInvert] ?? 0) != 0)
                let frames = platter.spin(turns: 0.5, steps: 40)
                XCTAssertTrue(frames.allSatisfy { StateFlags(rawValue: $0.flags).isDisjoint(with: .touched) },
                              "jogTotal counts the motion of an untouched platter (contract v1.4: all motion, whatever jogUntouched says)")
                for t in await receive(frames, from: dev, via: client) { wizard.feed(t) }
                let result = try wizard.finish()
                XCTAssertLessThan(result.netTicks, 0, "clockwise lowered jogTotal, so the setting is backwards")
                XCTAssertTrue(result.invert)
                XCTAssertEqual(result.configUpdates, [ConfigUpdate(key: .jogInvert, value: 1)])
                try await client.apply(result.configUpdates)
                XCTAssertEqual(config(dev, .jogInvert), 1, "the unit got SET_CONFIG jogInvert = 1")

                // Run it again with the unit as it is now: clockwise must count up, and the setting stays.
                platter.invert = true
                var again = PlatterDirectionCalibration(currentInvert: true)
                for t in await receive(platter.spin(turns: 0.5, steps: 40), from: dev, via: client) { again.feed(t) }
                let confirmed = try again.finish()
                XCTAssertGreaterThan(confirmed.netTicks, 0)
                XCTAssertEqual(confirmed.configUpdates, [ConfigUpdate(key: .jogInvert, value: 1)])

            case .holdPad:
                var wizard = HoldPadCalibration(rateHz: rate)
                var frames = platter.still(rate, jitter: 2)                         // hands off, 1 s
                frames += platter.still(5, jitter: 2)                               // a hand approaching
                frames += platter.spin(turns: 0.2, steps: rate, padRise: 40)        // hand on a turning platter
                frames += platter.still(6, jitter: 2)                               // lifted
                for t in await receive(frames, from: dev, via: client) { wizard.feed(t) }
                XCTAssertEqual(wizard.phase, .done)
                let result = try wizard.finish()
                XCTAssertEqual([result.onPermille, result.offPermille], [20, 12])
                try await client.apply(result.configUpdates)
                XCTAssertEqual(config(dev, .touchOnPermille), 20)
                XCTAssertEqual(config(dev, .touchOffPermille), 12)

            case .fader, .knob:
                var sweep = SweepCalibration(kind: step == .fader ? .fader : .knob)
                let raws = Array(stride(from: 100, through: 3900, by: 40))
                let frames = raws.map { step == .fader ? Telemetry(faderRaw: UInt16($0)) : Telemetry(knobRaw: UInt16($0)) }
                for t in await receive(frames, from: dev, via: client) { sweep.feed(t) }
                let result = try sweep.finish()
                try await client.apply(result.configUpdates)
                let keys: (ConfigKey, ConfigKey) = step == .fader ? (.faderRawMin, .faderRawMax) : (.knobRawMin, .knobRawMax)
                XCTAssertEqual(config(dev, keys.0), 138)    // 100 + 1% of 3800
                XCTAssertEqual(config(dev, keys.1), 3862)

            case .touchRing:
                XCTFail("the AS5600 flow has no ring step")
            }
        }

        XCTAssertEqual(dev.read { $0.saveCount }, 0, "nothing is permanent until Save")
        try await client.saveConfig()
        XCTAssertEqual(dev.read { $0.saveCount }, 1)
        let final = try await client.dumpConfig()
        XCTAssertEqual(final[.platterSource], 0, "calibrating did not change the platter source")
        XCTAssertEqual(final[.jogInvert], 1)
    }

    /// Contract v1.4: with `jogUntouched` SUPPRESS, or AUTO once the hold pad has registered a touch since power-up, the
    /// unit sends no jog for an untouched platter, so nothing arrives on the MIDI jog stream while the user spins it by
    /// hand (a fresh unit on AUTO would send it, which changes nothing here). The two wizards that need a spin must not
    /// care: the encoder check reads `Telemetry.angle`, the direction step reads `jogTotal`, which counts all motion.
    /// Neither looks at the touched flag or at `touchRaw`.
    func testTheEncoderAndDirectionStepsNeedNoHandOnTheHoldPadWhateverJogUntouchedSays() async throws {
        let dev = FakeDevice(deck: .a)
        let client = connect(dev)
        for policy in JogUntouched.allCases {
            try await client.setConfig(.jogUntouched, Int32(policy.rawValue))
            let settings = try await client.dumpConfig()
            XCTAssertEqual(settings[.jogUntouched], Int32(policy.rawValue))
            let info = try await client.getInfo()
            try await client.startStream(rateHz: rate)

            // No hand and no working pad: the touched flag is clear and the pad reads zero throughout.
            var platter = SimPlatter()
            let spin = platter.spin(turns: 1.5, steps: 75).map { frame -> Telemetry in
                var t = frame
                t.touchRaw = [UInt16](repeating: 0, count: 8)
                return t
            }
            XCTAssertTrue(spin.allSatisfy { StateFlags(rawValue: $0.flags).isDisjoint(with: .touched) && $0.touchRaw.allSatisfy { $0 == 0 } })
            let delivered = await receive(spin, from: dev, via: client)

            var diagnosis = EncoderDiagnosis(caps: Caps(rawValue: info.caps), rateHz: rate)
            var wizard = PlatterDirectionCalibration(currentInvert: false)
            for t in delivered {
                diagnosis.feed(t)
                wizard.feed(t)
            }
            XCTAssertTrue(diagnosis.verdict?.isOK ?? false, "\(policy): \(diagnosis.phase)")
            let result = try wizard.finish()
            XCTAssertGreaterThan(result.netTicks, 0, "\(policy)")
            XCTAssertFalse(result.invert, "\(policy)")
            try await client.stopStream()
        }
    }

    func testTheDirectionStepDependsOnJogTotalCountingAllMotion() throws {
        // The reverse of the test above: a unit whose jogTotal followed only the jog it SENT would sit still for an
        // untouched spin under SUPPRESS, and the step could not decide. This is why PROTOCOL.md section 2.2 says the
        // total counts everything, and what the end-to-end suite has to keep true of the real firmware.
        var platter = SimPlatter()
        var wizard = PlatterDirectionCalibration(currentInvert: false)
        for var t in platter.spin(turns: 0.5, steps: 40) {
            t.jogTotal = 0
            wizard.feed(t)
        }
        XCTAssertThrowsError(try wizard.finish()) { XCTAssertEqual($0 as? CalibrationError, .insufficientMotion(ticks: 0)) }
    }

    func testAUnitWhoseChipNeverAnsweredIsToldSoFromItsInfo() async throws {
        let dev = FakeDevice(deck: .a)
        dev.mutate { $0.info.caps &= ~Caps.as5600.rawValue }   // the boot probe found no chip
        let client = connect(dev)
        let info = try await client.getInfo()
        try await client.startStream(rateHz: rate)

        var platter = SimPlatter()
        var diagnosis = EncoderDiagnosis(caps: Caps(rawValue: info.caps), rateHz: rate)
        XCTAssertEqual(diagnosis.verdict, .noEncoder, "decided from Info before any telemetry")
        for t in await receive(platter.spin(turns: 2, steps: 80), from: dev, via: client) { diagnosis.feed(t) }
        XCTAssertEqual(diagnosis.verdict, .noEncoder)
    }

    func testAChipWithNoMagnetAnswersButTheAngleStaysPut() async throws {
        let dev = FakeDevice(deck: .a)
        let client = connect(dev)
        let info = try await client.getInfo()
        try await client.startStream(rateHz: rate)

        // The chip is on the bus and reads valid, but the platter turns above no magnet: the angle sits still.
        var diagnosis = EncoderDiagnosis(caps: Caps(rawValue: info.caps), rateHz: rate)
        let frames = (0..<500).map { i in
            Telemetry(seq: UInt8(truncatingIfNeeded: i), flags: StateFlags.angleValid.rawValue, angle: UInt16(1500 + i % 2))
        }
        for t in await receive(frames, from: dev, via: client) { diagnosis.feed(t) }
        guard case let .notMoving(span)? = diagnosis.verdict else { return XCTFail("\(diagnosis.phase)") }
        XCTAssertLessThanOrEqual(span, 1)
    }

    func testTheFlowFollowsThePlatterSourceTheUnitHolds() async throws {
        let dev = FakeDevice(deck: .a)
        let client = connect(dev)
        let expected: [(PlatterSource, [CalibrationStep])] = [
            (.touchRing, [.fader, .knob, .touchRing, .direction]),
            (.touchHoldOnly, [.holdPad, .fader, .knob]),
            (.as5600, [.encoder, .direction, .holdPad, .fader, .knob]),
        ]
        for (source, steps) in expected {
            try await client.setConfig(.platterSource, Int32(source.rawValue))
            let settings = try await client.dumpConfig()
            let info = try await client.getInfo()   // the FakeDevice's Info still says AS5600: the setting must win
            XCTAssertEqual(CalibrationFlow.steps(for: CalibrationFlow.source(config: settings[.platterSource], info: info.platterSource)),
                           steps, "\(source)")
        }

        try await client.loadDefaults()
        let reset = try await client.dumpConfig()
        XCTAssertEqual(CalibrationFlow.source(config: reset[.platterSource], info: nil), .as5600, "load defaults goes back to the AS5600 flow")
    }

    func testAHoldOnlyUnitCalibratesItsPadAndThenTheControls() async throws {
        let dev = FakeDevice(deck: .a)
        let client = connect(dev)
        try await client.setConfig(.platterSource, Int32(PlatterSource.touchHoldOnly.rawValue))
        try await client.startStream(rateHz: rate)

        var platter = SimPlatter()
        var wizard = HoldPadCalibration(rateHz: rate)
        var frames = platter.still(rate, jitter: 2)
        frames += platter.still(5, jitter: 2)
        frames += platter.still(rate, padRise: 60, jitter: 2)
        frames += platter.still(6, jitter: 2)
        for t in await receive(frames, from: dev, via: client) { wizard.feed(t) }
        let result = try wizard.finish()
        try await client.apply(result.configUpdates)
        XCTAssertEqual(config(dev, .touchOnPermille), Int32(result.onPermille))
        XCTAssertEqual(config(dev, .touchOffPermille), Int32(result.offPermille))
        XCTAssertGreaterThan(config(dev, .touchOnPermille), config(dev, .touchOffPermille), "hysteresis survives")
    }
}

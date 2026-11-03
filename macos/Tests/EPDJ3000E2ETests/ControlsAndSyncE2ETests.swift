// More of the contract against the real firmware: the response curves (implemented independently in firmware and
// Swift), host-set tempo, hot-plug, re-touching the platter elsewhere, and a unit following a clock.

import EPDJ3000Engine
import EPDJ3000Protocol
import XCTest

final class ControlsAndSyncE2ETests: E2ETestCase {
    /// The 14-bit value a unit settles on for a pot held at `raw` (ADC counts, no noise): what a receiver holds after the
    /// unit has said everything it had to say (inside a dead zone it says nothing new).
    private func settledFader(_ world: SimWorld, raw: Double) async throws -> Int {
        _ = try await world.command("fader u=0 raw=\(raw) noise=0")
        try await world.run(ms: 120)
        return try XCTUnwrap(world.faderValues().last, "no fader message at all")
    }

    func testFaderCurvesAndInvertAgreeBetweenTheFirmwareAndTheSwiftEngine() async throws {
        try await withWorld { world in
            let c = world.client().client
            // Positions far enough apart (> the 7-count gate) that each one is a real movement.
            let raws: [Double] = [40, 90, 180, 300, 500, 800, 1200, 1700, 2048, 2500, 3000, 3500, 3900, 4055]
            var linear = [Int]()
            for raw in raws { linear.append(try await settledFader(world, raw: raw)) }
            XCTAssertEqual(linear.first, 0)
            XCTAssertEqual(linear.last, 16383)
            // linear against the spec formula (PROTOCOL.md 2.4)
            for (raw, v) in zip(raws, linear) {
                let p = min(1, max(0, (raw - 40) / (4055 - 40)))
                let live = p <= 0.015 ? 0 : p >= 0.985 ? 1 : (p - 0.015) / 0.97
                XCTAssertEqual(Double(v), (live * 16383).rounded(), accuracy: 40, "linear at raw \(raw)")
            }
            for curve in [FaderCurve.sharpCut, .smooth] {
                try await c.setConfig(.faderCurve, Int32(curve.rawValue))
                try await world.run(ms: 50)
                for (i, raw) in raws.reversed().enumerated() {  // approach from the other side so every step is a movement
                    let v = try await settledFader(world, raw: raw)
                    let lin = linear[raws.count - 1 - i]
                    let expected = Double(curve.gain(at: Float(lin) / 16383)) * 16383
                    XCTAssertEqual(Double(v), expected, accuracy: 40, "\(curve) at raw \(raw): firmware \(v), Swift curve on the linear value \(lin) = \(Int(expected))")
                }
            }
            try await c.setConfig(.faderCurve, 0)
            try await c.setConfig(.faderInvert, 1)
            try await world.run(ms: 50)
            for (raw, lin) in zip(raws, linear) {
                let v = try await settledFader(world, raw: raw)
                XCTAssertEqual(Double(v), Double(16383 - lin), accuracy: 40, "inverted at raw \(raw)")
            }
        }
    }

    func testHostSetTempoIsClampedStoredAndNotEchoedBack() async throws {
        try await withWorld { world in
            let c = world.client()
            var state = ControllerState(deck: .a)
            for (sent, expected) in [(1285, 1285), (5000, Proto.bpmMaxX10), (100, Proto.bpmMinX10), (1200, 1200)] {
                let mark = world.process.log.count
                try await c.client.send(MidiEvent.cc14(.bpm, channel: 0, value: UInt16(sent)))
                try await world.run(ms: 150)
                let tempo = try await world.status().tempoX10
                XCTAssertEqual(tempo, expected, "host bpm \(sent)")
                let echoed = world.midiEvents(from: mark).contains {
                    if case let .controlChange(_, cc, _) = $0.event { return cc == Proto.CC.bpmMsb || cc == Proto.CC.bpmLsb }
                    return false
                }
                XCTAssertFalse(echoed)
            }
            // CC 21: one step = 0.1 BPM, relative offset-64
            try await c.client.send([.controlChange(channel: 0, controller: Proto.CC.bpmNudge, value: 64 + 5)])
            try await world.run(ms: 100)
            try await expectEqual(try await world.status().tempoX10, 1205)
            try await c.client.send([.controlChange(channel: 0, controller: Proto.CC.bpmNudge, value: 64 - 20)])
            try await world.run(ms: 100)
            try await expectEqual(try await world.status().tempoX10, 1185)
            // tap tempo from the host: four taps 600 ms apart
            for _ in 0..<5 {
                try await c.client.send([.noteOn(channel: 0, note: Proto.Note.tap, velocity: 127)])
                try await world.run(ms: 600)
            }
            let tapped = try await world.status().tempoX10
            XCTAssertEqual(Double(tapped), 1000, accuracy: 15, "tapping 100 BPM")
            // telemetry shows the tempo the unit would run (DJ mode: stored for display)
            try await c.client.startStream(rateHz: 50)
            try await world.run(ms: 100)
            let t = try XCTUnwrap(c.telemetry.take().last)
            XCTAssertEqual(Int(t.bpmX10), tapped)
            state.apply(c.events.take())
        }
    }

    func testReTouchingTheRingAtAnotherAngleNeverJumps() async throws {
        try await withWorld("units=1 platter=ring") { world in  // the experimental ring: the finger's angle is the platter's
            let c = world.client()
            var state = ControllerState(deck: .a)
            for rev in [0.05, 0.55, 0.30, 0.80, 0.55] {
                try await world.finger(rev: rev, pressure: 1)
                try await world.run(ms: 300)
                try await world.finger(pressure: 0)
                try await world.run(ms: 150)
            }
            state.apply(c.events.take())
            XCTAssertLessThanOrEqual(abs(state.jogTicks), 6, "five landings at wildly different angles moved the platter by \(state.jogTicks) ticks")
            XCTAssertFalse(state.touched)
        }
    }

    func testFingersThatLandAndLiftSlowlyDoNotMoveThePlatter() async throws {
        for args in ["units=1", "units=1 platter=ring"] {
            try await withWorld(args) { world in
                let c = world.client()
                var state = ControllerState(deck: .a)
                let mark = world.process.log.count
                for rampMs in [4, 20, 60, 150] {  // how long the fingertip takes to roll on and off
                    try await world.finger(rev: 0.7, pressure: 0)
                    try await world.ramp("pressure", from: 0, to: 1, ms: rampMs, slice: 4)
                    try await world.run(ms: 300)
                    try await world.ramp("pressure", from: 1, to: 0, ms: rampMs, slice: 4)
                    try await world.run(ms: 200)
                }
                state.apply(c.events.take())
                XCTAssertLessThanOrEqual(abs(state.jogTicks), 6, "landing and lifting moved the platter by \(state.jogTicks) ticks (\(args))")
                XCTAssertFalse(state.touched)
                if world.platter != .ring {
                    let n = world.touchNotes(from: mark)
                    XCTAssertEqual(n.downs, 4, "one touch note per landing on the hold pad, however slowly the finger rolled on")
                    XCTAssertEqual(n.ups, 4)
                }
            }
        }
    }

    func testPluggingTheUnitInTellsTheHostWhereTheSlidePotAndKnobAre() async throws {
        try await withWorld { world in
            let c = world.client()
            _ = try await world.command("usb u=0 mounted=0")
            _ = try await world.command("fader u=0 raw=3000 noise=0")
            _ = try await world.command("knob u=0 raw=1000 noise=0")
            try await world.run(ms: 500)
            _ = c.events.take()
            XCTAssertEqual(world.events().filter { $0.t > UInt64(4_700_000) }.count, 0, "nothing is sent with no host")
            _ = try await world.command("usb u=0 mounted=1")
            var state = ControllerState(deck: .a)
            try await world.run(ms: 700)  // plus the simulator's re-enumeration delay
            state.apply(c.events.take())
            try await c.client.startStream(rateHz: 50)
            try await world.run(ms: 100)
            let t = try XCTUnwrap(c.telemetry.take().last)
            XCTAssertEqual(state.fader, t.faderOut, "the fader position arrives on plug-in without a movement")
            XCTAssertEqual(state.knob, t.knobOut)
            XCTAssertGreaterThan(state.fader, 0)
        }
    }

    // MARK: following a clock

    func testSyncFollowLocksToAHostClockAndMirrorsItsTransport() async throws {
        try await withWorld { world in
            let c = world.client()
            try await c.client.setConfig(.mode, Int32(Mode.syncFollow.rawValue))
            try await c.client.startStream(rateHz: 50)
            try await world.run(ms: 100)

            // 140 BPM = 17.857 ms per clock; the host can only act on 1 ms boundaries here.
            let period = 60_000.0 / (140.0 * 24)
            var next = 0.0, elapsed = 0.0
            try await world.midi([0xFA])
            for _ in 0..<(24 * 6) {
                next += period
                let ms = Int(next.rounded()) - Int(elapsed.rounded())
                elapsed = next
                try await world.run(ms: ms)
                try await world.midi([0xF8])
            }
            try await world.run(ms: 20)
            let t = try XCTUnwrap(c.telemetry.take().last)
            XCTAssertEqual(Double(t.bpmX10), 1400, accuracy: 15, "the follower reports the tempo of the clock it hears")
            XCTAssertEqual(TransportState(rawValue: t.transport), .playing, "Start was mirrored")
            try await world.midi([0xFC])
            try await world.run(ms: 50)
            let stopped = try XCTUnwrap(c.telemetry.take().last)
            XCTAssertEqual(TransportState(rawValue: stopped.transport), .paused)
            // 500 ms with no clock: the tempo estimate is no longer valid and the unit shows its own again
            try await world.run(ms: 700)
            let silent = try XCTUnwrap(c.telemetry.take().last)
            XCTAssertEqual(Int(silent.bpmX10), 1200, "back to the configured tempo once the clock stops")
        }
    }

    func testAFollowerUnitTakesTempoAndTransportFromTheMasterOverTheRadio() async throws {
        try await withWorld("units=2 deck0=0 deck1=1") { world in
            let master = world.client(unit: 0, deck: 0)
            let follower = world.client(unit: 1, deck: 1)
            try await master.client.setConfig(.clockOutMask, Int32(ClockPorts.link.rawValue))
            try await master.client.setConfig(.bpmX10, 1285)
            try await follower.client.setConfig(.mode, Int32(Mode.syncFollow.rawValue))
            try await master.client.setConfig(.mode, Int32(Mode.syncMaster.rawValue))
            try await follower.client.startStream(rateHz: 50)
            try await world.run(ms: 2000)
            var t = try XCTUnwrap(follower.telemetry.take().last)
            XCTAssertEqual(Int(t.bpmX10), 1285, "tempo travels in TRANSPORT packets, not as 0xF8 bytes")
            XCTAssertTrue(StateFlags(rawValue: t.flags).contains(.linkUp))
            XCTAssertEqual(TransportState(rawValue: t.transport), .stopped)

            try await world.gesture(startRev: 0.3, revs: 0, ms: 1, dwellMs: 80, tailMs: 0, afterMs: 600, unit: 0)  // tap on the master
            t = try XCTUnwrap(follower.telemetry.take().last)
            XCTAssertEqual(TransportState(rawValue: t.transport), .playing, "the master's tap started the follower's view of the transport")
            try await world.gesture(startRev: 0.3, revs: 0, ms: 1, dwellMs: 80, tailMs: 0, afterMs: 600, unit: 0)
            t = try XCTUnwrap(follower.telemetry.take().last)
            XCTAssertNotEqual(TransportState(rawValue: t.transport), .playing)
            // no MIDI clock bytes ever crossed the radio: the follower's USB cable carries none
            XCTAssertTrue(world.events(unit: 1, cable: 0).allSatisfy { $0.bytes != [0xF8] })
        }
    }
}

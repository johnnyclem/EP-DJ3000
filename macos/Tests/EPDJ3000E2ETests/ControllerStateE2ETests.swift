// ControllerState (the pure fold of a unit's MIDI into one value) fed by the real DeviceClient with what the real
// firmware really sends for simulated hands: the platter, the slide pot, the knob, the buttons, the transport.

import EPDJ3000Protocol
import XCTest

final class ControllerStateE2ETests: E2ETestCase {
    private func fold(_ c: SimClient, into state: inout ControllerState) {
        state.apply(c.events.take())
    }

    // MARK: platter

    func testOneRevolutionFoldsIntoTheTicksTheFirmwareCountedAndAboutOneThousandAndTwentyFour() async throws {
        for (deck, startRev, revs) in [(Deck.a, 0.25, 1.0), (.a, 0.6, -1.0), (.b, 0.1, 1.0), (.b, 0.9, -1.0)] {
            try await withWorld("units=1 deck0=\(deck.rawValue)") { world in
                let c = world.client()
                var state = ControllerState(deck: deck)
                var wrongChannel = ControllerState(channel: deck == .a ? 1 : 0)
                var sawTouched = false

                try await world.gesture(startRev: startRev, revs: revs, ms: 1800, afterMs: 400)
                let events = c.events.take()
                for e in events {
                    state.apply(e)
                    wrongChannel.apply(e)
                    sawTouched = sawTouched || state.touched
                }
                let sim = try await world.status()
                XCTAssertEqual(state.jogTicks, sim.jogTotal, "\(deck) \(revs): the fold lost or invented ticks")
                XCTAssertEqual(wrongChannel.jogTicks, 0, "a state on the other channel ignores this unit")
                XCTAssertTrue(sawTouched)
                XCTAssertFalse(state.touched, "released")
                XCTAssertEqual(Double(state.jogTicks), 1024 * revs, accuracy: 24, "one revolution = 1024 ticks, \(revs > 0 ? "clockwise" : "counter-clockwise") sign")
                XCTAssertEqual(state.jogRevolutions, revs, accuracy: 0.025)
                observe("\(deck) one \(revs > 0 ? "CW" : "CCW") revolution: \(state.jogTicks) ticks (expected \(Int(1024 * revs)))")
            }
        }
    }

    func testFiveRevolutionsKeepTheAngleAndTheCountInStep() async throws {
        try await withWorld { world in
            let c = world.client()
            var state = ControllerState(deck: .a)
            try await world.gesture(revs: 5, ms: 8000, afterMs: 400)
            fold(c, into: &state)
            let sim = try await world.status()
            XCTAssertEqual(state.jogTicks, sim.jogTotal)
            XCTAssertEqual(state.jogRevolutions, 5, accuracy: 0.03)
            observe("five revolutions: \(state.jogTicks) ticks, \(String(format: "%.3f", state.jogRevolutions)) rev")
        }
    }

    func testJogMessagesObeyTheWireRules() async throws {
        try await withWorld { world in
            let mark = world.process.log.count
            try await world.gesture(revs: 2, ms: 1500, afterMs: 300)
            var last: UInt64 = 0
            var count = 0
            for (t, ev) in world.midiEvents(from: mark) {
                guard case let .controlChange(_, cc, value) = ev, cc == Proto.CC.jog else { continue }
                XCTAssertNotEqual(value, 64, "64 means no change and is never sent")
                XCTAssertNotEqual(value, 0)
                XCTAssertTrue((1...127).contains(Int(value)))
                if last != 0 { XCTAssertGreaterThanOrEqual(t - last, 2000, "at most one jog message every 2 ms") }
                last = t
                count += 1
            }
            XCTAssertGreaterThan(count, 100)
        }
    }

    // MARK: slide pot, knob, buttons

    func testFaderAndKnobFoldToTheValuesTheDeviceReportsInTelemetry() async throws {
        try await withWorld { world in
            let c = world.client()
            try await c.client.startStream(rateHz: 100)
            var state = ControllerState(deck: .a)
            for raw in [40.0, 1000, 2048, 3333, 4055] {
                _ = try await world.command("fader u=0 raw=\(raw) noise=0")
                _ = try await world.command("knob u=0 raw=\(Int(raw)) noise=0")
                try await world.run(ms: 150)
                fold(c, into: &state)
                let t = try XCTUnwrap(c.telemetry.take().last)
                XCTAssertEqual(state.fader, t.faderOut, "slide pot at raw \(raw): the 14-bit value on the wire is what the device computed")
                XCTAssertEqual(state.knob, t.knobOut, "knob at raw \(raw)")
                XCTAssertEqual(state.faderRole, .pitch, "contract v1.2: a factory unit's slide pot is the tempo fader")
            }
            XCTAssertEqual(state.faderNormalized, 1.0, accuracy: 1e-9, "top of travel")
            XCTAssertEqual(state.knobNormalized, 4055.0 / 4095.0, accuracy: 0.001)

            for role in [FaderRole.crossfader, .channelVolume] {
                try await c.client.setConfig(.faderRole, Int32(role.rawValue))
                try await world.run(ms: 50)
                fold(c, into: &state)
                XCTAssertEqual(state.faderRole, role, "the CC pair follows faderRole")
                let t = try XCTUnwrap(c.telemetry.take().last)
                XCTAssertEqual(state.fader, t.faderOut, "the new pair was sent the current position straight away")
            }

            // E2E-2: back to the role whose pair the fold has already seen, at the same position. The unit re-sends the
            // pair (it must tell the host what the pot means now) at the value the pair already held.
            let mark = world.process.log.count
            try await c.client.setConfig(.faderRole, Int32(FaderRole.pitch.rawValue))
            try await world.run(ms: 50)
            let resent = world.faderValues(since: mark)
            XCTAssertFalse(resent.isEmpty, "the firmware did re-send the pitch pair")
            fold(c, into: &state)
            XCTAssertEqual(state.faderRole, .pitch, "the role follows a pair that is sent again at an unchanged value")
            let t = try XCTUnwrap(c.telemetry.take().last)
            XCTAssertEqual(state.fader, t.faderOut)
        }
    }

    func testButtonsGestureAndTapPulsesInDJMode() async throws {
        try await withWorld { world in
            let c = world.client()
            var state = ControllerState(deck: .a)
            _ = try await world.command("buttons u=0 cue=1")
            try await world.run(ms: 40)
            fold(c, into: &state)
            XCTAssertEqual(state.buttons, [.cue])
            _ = try await world.command("buttons u=0 rev=1")
            try await world.run(ms: 40)
            fold(c, into: &state)
            XCTAssertEqual(state.buttons, [.cue, .rev])
            _ = try await world.command("buttons u=0 cue=0 rev=0")
            try await world.run(ms: 40)
            fold(c, into: &state)
            XCTAssertEqual(state.buttons, [])

            for n in 1...3 {
                try await world.gesture(startRev: 0.3, revs: 0, ms: 1, dwellMs: 80, tailMs: 0, afterMs: 150)
                fold(c, into: &state)
                XCTAssertEqual(state.playPulseCount, n, "tap \(n)")
            }
            XCTAssertEqual(state.tapCount, 0, "the tap-tempo note is a SYNC_MASTER thing")
            let simTotal = try await world.status().jogTotal
            XCTAssertEqual(state.jogTicks, simTotal)
        }
    }

    // MARK: SYNC_MASTER: transport, clock, tempo

    func testSyncMasterTransportClockAndTempoFoldTheWayTheDeviceSendsThem() async throws {
        try await withWorld { world in
            let c = world.client()
            var state = ControllerState(deck: .a)
            try await c.client.setConfig(.mode, Int32(Mode.syncMaster.rawValue))
            try await c.client.setConfig(.mtcEnable, 1)
            try await c.client.setConfig(.mtcFps, Int32(MtcFrameRate.fps30.rawValue))
            try await world.run(ms: 100)
            fold(c, into: &state)
            XCTAssertEqual(state.transport, .stopped)

            // Four CUE presses 600 ms apart tap a tempo of 100 BPM and the unit announces the change (it does not announce
            // a tap that lands on the tempo it already runs at, so 120 BPM would tell the host nothing).
            for _ in 0..<4 {
                _ = try await world.command("buttons u=0 cue=1")
                try await world.run(ms: 30)
                _ = try await world.command("buttons u=0 cue=0")
                try await world.run(ms: 570)
            }
            fold(c, into: &state)
            let tempo = try await world.status().tempoX10
            XCTAssertEqual(state.bpmX10, tempo, "ControllerState holds the tempo the unit runs at")
            XCTAssertEqual(Double(tempo), 1000, accuracy: 15)
            XCTAssertEqual(state.tapCount, 4)

            // A platter tap starts the transport: Start, then clocks (24 per quarter note) advance the position.
            try await world.gesture(startRev: 0.3, revs: 0, ms: 1, dwellMs: 80, tailMs: 0, afterMs: 100)
            fold(c, into: &state)
            XCTAssertEqual(state.transport, .playing)
            let start = state.positionClocks
            try await world.run(ms: 2000)
            fold(c, into: &state)
            let clocks = state.positionClocks - start
            let expected = 2.0 * Double(tempo) / 10.0 / 60.0 * 24
            XCTAssertEqual(Double(clocks), expected, accuracy: 3, "2 s of clock at \(Double(tempo) / 10) BPM")
            let timecode = try XCTUnwrap(state.timecode, "MTC while playing")
            XCTAssertEqual(timecode.rate, .fps30)
            let shown = Double(timecode.minutes * 60 + timecode.seconds) + Double(timecode.frames) / 30
            XCTAssertEqual(shown, 2.0, accuracy: 0.25, "MTC shows the time since Start (2 s of play, 2 frames of transmission delay)")

            // A second tap pauses: Stop + Song Position, so Continue can resume exactly there.
            let beforePause = state.positionClocks
            try await world.gesture(startRev: 0.3, revs: 0, ms: 1, dwellMs: 80, tailMs: 0, afterMs: 100)
            fold(c, into: &state)
            XCTAssertEqual(state.transport, .paused)
            XCTAssertEqual(state.positionClocks % 6, 0, "SPP is in whole MIDI beats")
            XCTAssertLessThanOrEqual(state.positionClocks, beforePause + 6)
            XCTAssertGreaterThanOrEqual(state.positionClocks, beforePause - 6)
            let simAfterPause = try await world.status()
            XCTAssertEqual(simAfterPause.transport, Int(TransportState.paused.rawValue))

            // REV = stop + rewind
            _ = try await world.command("buttons u=0 rev=1")
            try await world.run(ms: 40)
            _ = try await world.command("buttons u=0 rev=0")
            try await world.run(ms: 100)
            fold(c, into: &state)
            XCTAssertEqual(state.transport, .stopped, "Stop followed by SPP 0 mirrors as STOPPED")
            XCTAssertEqual(state.positionClocks, 0)
            let simStopped = try await world.status()
            XCTAssertEqual(simStopped.transport, Int(TransportState.stopped.rawValue))
        }
    }

    func testPlatterSpinNudgesTempoInSyncMasterAndKnobSetsItWhenKnobRoleIsBpm() async throws {
        try await withWorld { world in
            let c = world.client()
            var state = ControllerState(deck: .a)
            try await c.client.setConfig(.mode, Int32(Mode.syncMaster.rawValue))
            try await world.run(ms: 600)
            fold(c, into: &state)
            let before = try await world.status().tempoX10

            // +0.1 BPM per 32 ticks clockwise: three quarters of a turn is 768 ticks = 24 steps = +2.4 BPM
            try await world.gesture(startRev: 0.25, revs: 0.75, ms: 1500, afterMs: 300)
            fold(c, into: &state)
            let after = try await world.status().tempoX10
            XCTAssertEqual(state.bpmX10, after)
            XCTAssertEqual(Double(after - before), 24, accuracy: 3, "0.75 rev clockwise = +2.4 BPM")
            XCTAssertEqual(state.jogTicks, 0, "in SYNC_MASTER the platter is a tempo wheel, no jog is sent")

            try await c.client.setConfig(.knobRole, Int32(KnobRole.bpm.rawValue))
            _ = try await world.command("knob u=0 raw=2048 noise=0")
            try await world.run(ms: 200)
            _ = try await world.command("knob u=0 raw=3071 noise=0")  // 3/4 of the travel
            try await world.run(ms: 300)
            fold(c, into: &state)
            let knobTempo = try await world.status().tempoX10
            XCTAssertEqual(Double(knobTempo), 600 + 1200 * 3071.0 / 4095.0, accuracy: 6, "60 + 120 x position")
            XCTAssertEqual(state.bpmX10, knobTempo)
        }
    }

    // MARK: other platter sources

    func testFreeSpinningAS5600PlatterIsCountedExactlyAcrossTheWrapAndWithEncoderNoise() async throws {
        for (start, revs, noise) in [(0.0, 1.0, 0.0), (0.9, 0.3, 0.0), (0.05, -1.0, 0.0), (0.0, 3.0, 1.5), (0.7, -2.0, 1.5)] {
            try await withWorld("units=1 as5600=1") { world in
                let c = world.client()
                var state = ControllerState(deck: .a)
                try await c.client.setConfig(.platterSource, Int32(PlatterSource.as5600.rawValue))
                _ = try await world.command("as5600 u=0 rev=\(start) noise=\(noise)")
                try await world.run(ms: 100)
                _ = c.events.take()
                let before = try await world.status().jogTotal
                try await world.ramp("as5600", from: start, to: start + revs, ms: Int(abs(revs) * 1000))
                try await world.run(ms: 100)  // the 500 Hz emitter flushes
                state.apply(c.events.take())
                let sim = try await world.status()
                XCTAssertEqual(state.jogTicks, sim.jogTotal - before, "start \(start) revs \(revs) noise \(noise): wire vs firmware count")
                XCTAssertEqual(Double(state.jogTicks), 1024 * revs, accuracy: 1.5, "the encoder's net travel, whatever the noise and wrap")
                XCTAssertFalse(state.touched, "free-spinning: nobody is touching it")
            }
        }
    }

    func testTouchHoldOnlySendsTouchAndTapButNeverMotion() async throws {
        try await withWorld { world in
            let c = world.client()
            var state = ControllerState(deck: .a)
            try await c.client.setConfig(.platterSource, Int32(PlatterSource.touchHoldOnly.rawValue))
            try await world.run(ms: 100)
            _ = c.events.take()
            try await world.gesture(revs: 1.5, ms: 2500)
            state.apply(c.events.take())
            XCTAssertEqual(state.jogTicks, 0)
            try await expectEqual(try await world.status().jogTotal, 0)
            try await world.gesture(startRev: 0.3, revs: 0, ms: 1, dwellMs: 80, tailMs: 0, afterMs: 150)
            state.apply(c.events.take())
            XCTAssertEqual(state.playPulseCount, 1, "a tap still plays")
        }
    }

    func testRequestsAreNotConfusedByTelemetryFloodingTheSamePort() async throws {
        try await withWorld { world in
            let c = world.client()
            try await c.client.startStream(rateHz: 100)
            try await world.gesture(revs: 1, ms: 1200, afterMs: 100)
            for _ in 0..<3 {
                let dump = try await c.client.dumpConfig()
                XCTAssertEqual(dump.count, ConfigKey.allCases.count)
                _ = try await c.client.getInfo()
                try await c.client.setConfig(.ledBrightness, 10)
                try await world.run(ms: 50)
            }
            XCTAssertGreaterThan(c.telemetry.total, 100)
            let rejected = await c.client.rejectedFrameCount
            XCTAssertEqual(rejected, 0, "the client dropped no frame the firmware sent")
        }
    }

    // MARK: tap rules, buttons, chords, clock quality

    func testTapRulesFollowTheConfigAndThePulseIsLongEnoughToSee() async throws {
        try await withWorld { world in
            let c = world.client()
            var state = ControllerState(deck: .a)
            let mark = world.process.log.count

            // 150 ms, still: a tap. 250 ms, still: not a tap (tapMaxMs = 180).
            try await world.gesture(startRev: 0.3, revs: 0, ms: 1, dwellMs: 150, tailMs: 0, afterMs: 200)
            try await world.gesture(startRev: 0.3, revs: 0, ms: 1, dwellMs: 250, tailMs: 0, afterMs: 200)
            // 80 ms but sliding a tenth of a turn (102 ticks, tapMoveTicks = 8): not a tap.
            try await world.gesture(startRev: 0.3, revs: 0.1, ms: 80, dwellMs: 10, tailMs: 0, afterMs: 200)
            state.apply(c.events.take())
            XCTAssertEqual(state.playPulseCount, 1, "only the short still touch counted")

            // The note-off follows the note-on by at least 10 ms (PROTOCOL.md 2.3).
            let pulse = world.midiEvents(from: mark).compactMap { (t, ev) -> (UInt64, Bool)? in
                switch ev {
                case let .noteOn(_, n, _) where n == Proto.Note.play: return (t, true)
                case let .noteOff(_, n, _) where n == Proto.Note.play: return (t, false)
                default: return nil
                }
            }
            XCTAssertEqual(pulse.map(\.1), [true, false])
            XCTAssertGreaterThanOrEqual(pulse[1].0 - pulse[0].0, 10_000)

            // tapMaxMs and tapMoveTicks are live-configurable, tapPlay = 0 turns the tap off.
            try await c.client.setConfig(.tapMaxMs, 400)
            try await world.gesture(startRev: 0.3, revs: 0, ms: 1, dwellMs: 250, tailMs: 0, afterMs: 200)
            state.apply(c.events.take())
            XCTAssertEqual(state.playPulseCount, 2, "a 250 ms touch is a tap once tapMaxMs = 400")
            try await c.client.setConfig(.tapPlay, 0)
            try await world.gesture(startRev: 0.3, revs: 0, ms: 1, dwellMs: 100, tailMs: 0, afterMs: 200)
            state.apply(c.events.take())
            XCTAssertEqual(state.playPulseCount, 2, "tapPlay = 0")
        }
    }

    func testButtonsAreDebouncedAndTheHeldChordCyclesTheModeThenSwapsTheDeck() async throws {
        try await withWorld { world in
            let c = world.client()
            var state = ControllerState(deck: .a)
            // Contact bounce inside the 4 ms lockout (down, up, down, up, down) is one press, reported with no delay.
            let mark = world.process.log.count
            for v in [1, 0, 1, 0, 1] {
                _ = try await world.command("buttons u=0 cue=\(v)")
                try await world.run(ms: 1)
            }
            try await world.run(ms: 40)
            _ = try await world.command("buttons u=0 cue=0")
            try await world.run(ms: 40)
            let cueEdges = world.midiEvents(from: mark).compactMap { (_, ev) -> Bool? in
                switch ev {
                case let .noteOn(_, n, _) where n == Proto.Note.cue: return true
                case let .noteOff(_, n, _) where n == Proto.Note.cue: return false
                default: return nil
                }
            }
            XCTAssertEqual(cueEdges, [true, false], "one press and one release, not five edges")
            state.apply(c.events.take())

            // CUE + REV held 2.5 s then released: DJ -> SYNC_MASTER, with note-offs for what had been reported down
            _ = try await world.command("buttons u=0 cue=1 rev=1")
            try await world.run(ms: 100)
            state.apply(c.events.take())
            XCTAssertEqual(state.buttons, [.cue, .rev], "single presses are not delayed to wait for a chord")
            try await world.run(ms: 2400)
            _ = try await world.command("buttons u=0 cue=0 rev=0")
            try await world.run(ms: 100)
            state.apply(c.events.take())
            XCTAssertEqual(state.buttons, [])
            try await expectEqual(try await c.client.getInfo().mode, Mode.syncMaster.rawValue)
            try await expectEqual(try await c.client.getConfig(.mode), Int32(Mode.syncMaster.rawValue))

            // held >= 5 s: swap A <-> B, save, restart
            _ = try await world.command("buttons u=0 cue=1 rev=1")
            try await world.run(ms: 5200)
            _ = try await world.command("buttons u=0 cue=0 rev=0")
            try await world.run(ms: 1500)  // restart + re-enumeration
            let sim = try await world.status()
            XCTAssertEqual(sim.stored[Int(ConfigKey.deckId.rawValue)], 1, "the swap was stored")
            XCTAssertEqual(sim.int("reboots"), 1)
            let info = try await c.client.getInfo()
            XCTAssertEqual(info.deck, 1, "now Deck B")
        }
    }

    func testMidiClockHoldsItsTempoWithoutDriftAndFollowsClockOutMask() async throws {
        try await withWorld { world in
            let c = world.client()
            try await c.client.setConfig(.bpmX10, 1285)
            try await c.client.setConfig(.mode, Int32(Mode.syncMaster.rawValue))
            try await world.run(ms: 200)
            let mark = world.process.log.count
            try await world.run(ms: 10_000)
            let clocks = world.process.log.dropFirst(mark).filter { $0.bytes == [0xF8] && $0.ports & 1 != 0 }
            let expected = 10.0 * 128.5 / 60 * 24  // 514 ticks
            XCTAssertEqual(Double(clocks.count), expected, accuracy: 1.5, "no drift over 10 s: the schedule is the exact one")
            let gaps = zip(clocks, clocks.dropFirst()).map { Double($1.t - $0.t) / 1000 }
            let period = 60_000.0 / (128.5 * 24)
            XCTAssertLessThan(gaps.map { abs($0 - period) }.max() ?? 0, 1.01, "every tick within one 1 ms engine step of its ideal time (period \(String(format: "%.2f", period)) ms)")

            // TRS only
            try await c.client.setConfig(.clockOutMask, Int32(ClockPorts.trs.rawValue))
            try await world.run(ms: 100)
            let mark2 = world.process.log.count
            try await world.run(ms: 1000)
            let tail = world.process.log.dropFirst(mark2).filter { $0.bytes == [0xF8] }
            XCTAssertGreaterThan(tail.count, 40)
            XCTAssertTrue(tail.allSatisfy { $0.ports == Int(ClockPorts.trs.rawValue) }, "clock on the TRS jack only")
            // none
            try await c.client.setConfig(.clockOutMask, 0)
            try await world.run(ms: 100)
            let mark3 = world.process.log.count
            try await world.run(ms: 500)
            XCTAssertTrue(world.process.log.dropFirst(mark3).filter { $0.bytes == [0xF8] }.isEmpty)
        }
    }

    func testAFlickFasterThanTheWireCanCarryIsDeliveredLateButNeverLost() async throws {
        try await withWorld("units=1 as5600=1") { world in
            let c = world.client()
            var state = ControllerState(deck: .a)
            try await c.client.setConfig(.platterSource, Int32(PlatterSource.as5600.rawValue))
            _ = try await world.command("as5600 u=0 rev=0 noise=0")
            try await world.run(ms: 100)
            _ = c.events.take()
            let before = try await world.status().jogTotal
            // 30 revolutions in 0.5 s: 61 440 ticks/s, twice what 63 ticks per 2 ms can carry
            try await world.ramp("as5600", from: 0, to: 30, ms: 500)
            let tSpinEnd = world.virtualSeconds
            try await world.run(ms: 2500)
            state.apply(c.events.take())
            let sim = try await world.status()
            XCTAssertEqual(sim.jogTotal - before, 30 * 1024, "the firmware counted all of it")
            XCTAssertEqual(state.jogTicks, sim.jogTotal - before, "and the wire carried all of it, late")
            let lastJog = world.midiEvents().last { if case let .controlChange(_, cc, _) = $0.event { return cc == Proto.CC.jog } else { return false } }
            observe("30 revolutions in 0.5 s: the last jog message left \(String(format: "%.2f", Double(lastJog!.t) / 1e6 - tSpinEnd)) s after the spin ended")
        }
    }
}

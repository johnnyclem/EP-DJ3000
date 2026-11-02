// DeckModel + ScratchPlayerCore (the app's scratch player) driven by what the real firmware sends for simulated hands:
// touch holds the track, the platter scratches it, a tap plays and pauses, and the deck's LED state goes back into
// the unit and is accepted by it.

import EPDJ3000Engine
import EPDJ3000Protocol
import Foundation
import XCTest

/// A deck as the app wires it: unit -> MIDI -> DeckModel -> ScratchPlayerCore, rendered the way an audio callback would,
/// 96 frames (2 ms) per block, in lock step with the simulator's virtual time. Feedback goes back to the unit.
private final class DeckBench {
    let world: SimWorld
    let unit: Int
    let player = ScratchPlayerCore(sampleRate: 48_000)
    let deck: DeckModel
    private var left = [Float](repeating: 0, count: 96)
    private var right = [Float](repeating: 0, count: 96)
    private(set) var peak: Float = 0
    var sendFeedback = true

    init(world: SimWorld, unit: Int = 0, deck id: Deck = .a, trackSeconds: Double = 12) {
        self.world = world
        self.unit = unit
        let n = Int(trackSeconds * 48_000)
        let samples = (0..<n).map { Float(0.8 * sin(2 * Double.pi * 440 * Double($0) / 48_000)) }
        player.load(AudioBuffer(mono: samples, sampleRate: 48_000))
        deck = DeckModel(player: player, deck: id)
        render()  // the render thread picks the track up at the start of a block
    }

    var status: PlayerStatus { player.status }

    private func render() {
        left.withUnsafeMutableBufferPointer { l in
            right.withUnsafeMutableBufferPointer { r in player.render(left: l.baseAddress!, right: r.baseAddress!, frameCount: 96) }
        }
        peak = max(peak, left.map { abs($0) }.max() ?? 0)
    }

    private func block(_ reply: SimReply) async throws {
        for e in reply.events where e.unit == unit && e.ports & 1 != 0 && e.cable == 0 && e.kind == "msg" {
            var parser = MidiStreamParser()
            deck.handle(parser.feed(e.bytes), at: Double(e.t) / 1_000_000)
        }
        render()
        if sendFeedback {
            for event in deck.feedback() { try await world.midi(event.bytes, unit: unit) }
        }
    }

    /// Advances `ms` (even) milliseconds with the sensors as they are.
    func run(ms: Int) async throws {
        for _ in 0..<(ms / 2) { try await block(try await world.process.run(ms: 2)) }
    }

    func ramp(_ what: String, from: Double, to: Double, ms: Int) async throws {
        let blocks = ms / 2
        for i in 0..<blocks {
            let a = from + (to - from) * Double(i) / Double(blocks)
            let b = from + (to - from) * Double(i + 1) / Double(blocks)
            try await block(try await world.process.command("ramp u=\(unit) what=\(what) from=\(a) to=\(b) ms=2"))
        }
    }

    func finger(rev: Double? = nil, pressure: Double? = nil) async throws { try await world.finger(rev: rev, pressure: pressure, unit: unit) }

    /// A platter tap: touch for 80 ms without moving, release.
    func tap(rev: Double = 0.3) async throws {
        try await finger(rev: rev, pressure: 1)
        try await run(ms: 80)
        try await finger(pressure: 0)
        try await run(ms: 120)
    }

    func seek(_ seconds: Double) async throws {
        player.send(.seek(seconds: seconds))
        try await run(ms: 40)
    }
}

final class DeckE2ETests: E2ETestCase {
    private let nominalFramesPerRev = 1024.0 / ScratchConstants.nominalTicksPerSecond * 48_000  // 86400: 1.8 s of track

    func testTapPlaysAndPausesAndTheUnitsLEDFollows() async throws {
        try await withWorld { world in
            let bench = DeckBench(world: world)
            try await bench.seek(3)
            try await bench.tap()
            XCTAssertEqual(bench.status.transport, .playing, "the unit's play pulse toggled the player")
            try await bench.run(ms: 20)
            let led = try await world.status().led
            XCTAssertTrue(led.playing, "and the play LED note went back into the unit and lit it")
            try await bench.tap()
            XCTAssertEqual(bench.status.transport, .paused)
            try await bench.run(ms: 20)
            let off = try await world.status().led
            XCTAssertFalse(off.playing)
            try await bench.tap()
            XCTAssertEqual(bench.status.transport, .playing)
            XCTAssertEqual(bench.deck.controller.playPulseCount, 3)
        }
    }

    func testTouchHoldsAPlayingTrackAndReleaseLetsItGoOnAtNominalSpeed() async throws {
        try await withWorld { world in
            let bench = DeckBench(world: world)
            try await bench.seek(3)
            try await bench.tap()
            try await bench.run(ms: 600)
            XCTAssertEqual(bench.status.speed, 1.0, accuracy: 0.02, "playing at nominal speed")
            let t0 = bench.status.playheadFrames
            try await bench.run(ms: 500)
            XCTAssertEqual(bench.status.playheadFrames - t0, 24_000, accuracy: 600, "500 ms of audio")

            // A still finger on the platter holds the record: how long until it stands still?
            try await bench.finger(rev: 0.4, pressure: 1)
            var stoppedAfterMs: Int?
            for block in 1...60 {
                try await bench.run(ms: 2)
                if stoppedAfterMs == nil, abs(bench.status.speed) < 0.05 { stoppedAfterMs = block * 2 }
            }
            observe("a finger landing on a record playing at 1x: touch detected and speed under 0.05 after \(stoppedAfterMs ?? -1) ms")
            XCTAssertLessThanOrEqual(try XCTUnwrap(stoppedAfterMs), 40, "the record must be held within 40 ms of the finger landing")
            XCTAssertTrue(bench.status.touched)
            let held0 = bench.status.playheadFrames
            try await bench.run(ms: 800)
            let drift = abs(bench.status.playheadFrames - held0)
            observe("finger resting on a playing deck for 0.8 s: playhead moved \(Int(drift)) frames (\(String(format: "%.2f", drift / 48)) ms of audio)")
            XCTAssertLessThan(drift, 480, "held within 10 ms of audio over 0.8 s")
            XCTAssertEqual(bench.status.speed, 0, accuracy: 0.02)

            // Release: back to playing.
            try await bench.finger(pressure: 0)
            try await bench.run(ms: 500)
            XCTAssertFalse(bench.status.touched)
            XCTAssertEqual(bench.status.speed, 1.0, accuracy: 0.02)
            let before = bench.status.playheadFrames
            try await bench.run(ms: 500)
            XCTAssertEqual(bench.status.playheadFrames - before, 24_000, accuracy: 600)
        }
    }

    func testScratchingMovesTheTrackByTheLawOneRevolutionIsOnePointEightSeconds() async throws {
        try await withWorld { world in
            let bench = DeckBench(world: world)
            try await bench.seek(5)
            let start = bench.status.playheadFrames

            // One clockwise revolution at 33 1/3 rpm on a paused deck: forwards by 1.8 s, and it stops when let go.
            try await bench.finger(rev: 0.25, pressure: 1)
            try await bench.run(ms: 60)
            try await bench.ramp("finger", from: 0.25, to: 1.25, ms: 1800)
            try await bench.run(ms: 60)
            try await bench.finger(pressure: 0)
            try await bench.run(ms: 400)
            let cw = bench.status.playheadFrames - start
            observe("one clockwise revolution moved the track by \(String(format: "%.3f", cw / 48_000)) s (law: \(String(format: "%.3f", nominalFramesPerRev / 48_000)) s)")
            XCTAssertEqual(cw, nominalFramesPerRev, accuracy: nominalFramesPerRev * 0.04, "forwards by 1.8 s")
            XCTAssertEqual(bench.status.speed, 0, accuracy: 0.01, "and it stops when let go")
            XCTAssertEqual(bench.status.transport, .stopped, "scratching a stopped deck does not start it")

            // Half a revolution counter-clockwise at four times that speed: backwards by 0.9 s.
            let mid = bench.status.playheadFrames
            try await bench.finger(rev: 0.5, pressure: 1)
            try await bench.run(ms: 60)
            try await bench.ramp("finger", from: 0.5, to: 0.0, ms: 225)
            try await bench.run(ms: 60)
            try await bench.finger(pressure: 0)
            try await bench.run(ms: 400)
            let ccw = bench.status.playheadFrames - mid
            observe("half a counter-clockwise turn at 4x moved the track by \(String(format: "%.3f", ccw / 48_000)) s")
            XCTAssertEqual(ccw, -nominalFramesPerRev / 2, accuracy: nominalFramesPerRev * 0.05)
        }
    }

    func testTheLawHoldsAtSeveralSpeedsAndTheTrackNeverMovesMoreThanThePlatterDid() async throws {
        for (revsPerSecond, revs) in [(0.28, 0.5), (0.56, 1.0), (1.1, 1.0), (2.2, 1.5)] {
            try await withWorld { world in
                let bench = DeckBench(world: world)
                try await bench.seek(5)
                let start = bench.status.playheadFrames
                try await bench.finger(rev: 0.25, pressure: 1)
                try await bench.run(ms: 60)
                try await bench.ramp("finger", from: 0.25, to: 0.25 + revs, ms: Int(revs / revsPerSecond * 1000))
                try await bench.run(ms: 60)
                try await bench.finger(pressure: 0)
                try await bench.run(ms: 400)
                let moved = (bench.status.playheadFrames - start) / 48_000
                let ticks = Double(bench.deck.controller.jogTicks)
                let expected = ticks / ScratchConstants.nominalTicksPerSecond
                observe("\(revsPerSecond) rev/s x \(revs) rev: \(Int(ticks)) ticks -> track moved \(String(format: "%.3f", moved)) s, law says \(String(format: "%.3f", expected)) s (\(String(format: "%+.1f", (moved / expected - 1) * 100)) %)")
                XCTAssertEqual(moved, expected, accuracy: expected * 0.05 + 0.02, "\(revsPerSecond) rev/s")
            }
        }
    }

    func testABabyScratchGoesForwardsAndBackAndEndsWhereItStarted() async throws {
        try await withWorld { world in
            let bench = DeckBench(world: world)
            try await bench.seek(5)
            let start = bench.status.playheadFrames
            try await bench.finger(rev: 0.25, pressure: 1)
            try await bench.run(ms: 60)
            for _ in 0..<4 {
                try await bench.ramp("finger", from: 0.25, to: 0.40, ms: 120)
                try await bench.ramp("finger", from: 0.40, to: 0.25, ms: 120)
            }
            try await bench.run(ms: 60)
            try await bench.finger(pressure: 0)
            try await bench.run(ms: 400)
            let net = bench.status.playheadFrames - start
            observe("four forward-and-back scratches ended \(Int(net)) frames from where they started (a stroke is \(Int(0.15 * nominalFramesPerRev)) frames)")
            XCTAssertLessThan(abs(net), 0.15 * nominalFramesPerRev * 0.25, "net displacement under a quarter of one stroke")
        }
    }

    func testCueSetsAndReturnsAndTheCueLEDFollowsThePlayer() async throws {
        try await withWorld { world in
            let bench = DeckBench(world: world)
            try await bench.seek(2)
            let cuePoint = bench.status.playheadFrames
            _ = try await world.command("buttons u=0 cue=1")
            try await bench.run(ms: 400)
            XCTAssertTrue(bench.status.previewing, "CUE on a paused deck previews from the cue point while held")
            XCTAssertGreaterThan(bench.status.playheadFrames - cuePoint, 15_000)
            let lit = try await world.status().led
            XCTAssertTrue(lit.cueLit, "the player says the cue is lit, and so does the unit once told")
            _ = try await world.command("buttons u=0 cue=0")
            try await bench.run(ms: 200)
            XCTAssertFalse(bench.status.previewing)
            XCTAssertEqual(bench.status.playheadFrames, cuePoint, accuracy: 100, "released: back on the cue point")
            let parked = try await world.status().led
            XCTAssertEqual(parked.cueLit, bench.status.cueLit, "parked on the cue point the LED stays lit")
        }
    }

    func testRevPlaysBackwardsWhileHeld() async throws {
        try await withWorld { world in
            let bench = DeckBench(world: world)
            try await bench.seek(5)
            try await bench.tap()
            try await bench.run(ms: 300)
            _ = try await world.command("buttons u=0 rev=1")
            try await bench.run(ms: 300)
            XCTAssertEqual(bench.status.speed, -1.0, accuracy: 0.05)
            _ = try await world.command("buttons u=0 rev=0")
            try await bench.run(ms: 300)
            XCTAssertEqual(bench.status.speed, 1.0, accuracy: 0.05)
        }
    }

    func testSlidePotKnobAndPitchRoleDriveGainVolumeAndTempo() async throws {
        try await withWorld { world in
            let bench = DeckBench(world: world)
            let c = world.client()
            // default faderRole = PITCH (contract v1.2): top of travel = +8 %, bottom = -8 %, middle = nominal
            for (raw, ratio) in [(4055.0, 1.08), (40.0, 0.92), (2048.0, 1.0)] {
                _ = try await world.command("fader u=0 raw=\(raw) noise=0")
                try await bench.run(ms: 100)
                XCTAssertEqual(bench.deck.tempoRatio, ratio, accuracy: 0.003, "pitch pot at raw \(raw)")
            }
            XCTAssertEqual(bench.deck.controller.faderRole, .pitch)

            // CHANNEL_VOLUME, selected explicitly: the pot is the deck's own volume, linear
            try await c.client.setConfig(.faderRole, Int32(FaderRole.channelVolume.rawValue))
            for (raw, gain) in [(4055.0, 1.0), (40.0, 0.0), (2048.0, 0.5)] {
                _ = try await world.command("fader u=0 raw=\(raw) noise=0")
                try await bench.run(ms: 200)
                XCTAssertEqual(Double(bench.status.gain), gain, accuracy: 0.02, "pot at raw \(raw)")
            }
            XCTAssertEqual(bench.deck.controller.faderRole, .channelVolume)

            _ = try await world.command("knob u=0 raw=1024 noise=0")
            try await bench.run(ms: 100)
            XCTAssertEqual(Double(bench.player.parameters.volume), 0.25, accuracy: 0.01, "the knob is the output trim")
        }
    }

    func testHostFeedbackForTheOtherDeckAndForTheSameDeckOnAnotherChannelIsIgnoredByTheUnit() async throws {
        try await withWorld { world in
            // A host that sends both decks' LEDs to every port must not light Deck A with Deck B's state.
            let b = DeckModel(player: ScratchPlayerCore(sampleRate: 48_000), channel: 1)
            b.player.load(AudioBuffer(mono: [Float](repeating: 0.1, count: 48_000), sampleRate: 48_000))
            b.player.send(.play)
            b.player.send(.setCueHere)
            var scratch = [Float](repeating: 0, count: 96), scratch2 = scratch
            scratch.withUnsafeMutableBufferPointer { l in
                scratch2.withUnsafeMutableBufferPointer { r in b.player.render(left: l.baseAddress!, right: r.baseAddress!, frameCount: 96) }
            }
            let feedback = b.feedback()
            XCTAssertFalse(feedback.isEmpty)
            for event in feedback {
                XCTAssertEqual(event.channel, 1)
                try await world.midi(event.bytes)
            }
            try await world.run(ms: 10)
            let st = try await world.status().led
            XCTAssertFalse(st.playing, "Deck A's play LED ignores a channel-2 note")
            XCTAssertFalse(st.cueLit)
            // and a transport command for the other channel does not move it either
            try await world.midi([0xB1, Proto.CC.transport, TransportState.playing.rawValue])
            try await world.run(ms: 10)
            let after = try await world.status().led
            XCTAssertFalse(after.playing)
        }
    }

    func testInSyncMasterATapIsOneToggleAndHostPlayFeedbackNeverStartsTheUnitsClock() async throws {
        try await withWorld { world in
            let c = world.client()
            try await c.client.setConfig(.mode, Int32(Mode.syncMaster.rawValue))
            let bench = DeckBench(world: world)
            try await bench.seek(3)
            try await bench.run(ms: 200)

            // The unit's tap arrives as Start AND as a play pulse; the player must toggle once.
            try await bench.tap()
            XCTAssertEqual(bench.status.transport, .playing, "one tap, one toggle")
            try await expectEqual(try await world.status().transport, Int(TransportState.playing.rawValue))
            try await bench.tap()
            XCTAssertEqual(bench.status.transport, .paused)
            try await bench.tap()
            XCTAssertEqual(bench.status.transport, .playing)

            // REV = stop + rewind: Stop, SPP 0. The player stops (returns to its cue point).
            _ = try await world.command("buttons u=0 rev=1")
            try await bench.run(ms: 100)
            _ = try await world.command("buttons u=0 rev=0")
            try await bench.run(ms: 300)
            XCTAssertEqual(bench.status.transport, .stopped)
            try await expectEqual(try await world.status().transport, Int(TransportState.stopped.rawValue))

            // The app starting its own deck sends the play LED note; the unit must treat it as feedback only.
            let mark = world.process.log.count
            bench.player.send(.play)
            try await bench.run(ms: 200)
            XCTAssertEqual(bench.status.transport, .playing)
            try await expectEqual(try await world.status().transport, Int(TransportState.stopped.rawValue), "the unit's clock did not start")
            let started = world.midiEvents(from: mark).contains { if case .start = $0.event { return true } else { return false } }
            XCTAssertFalse(started, "no MIDI Start from a play note")
            // PROTOCOL.md 2.6: the note is "the host telling the unit its deck is playing, for the LED ... in every mode".
            // The LED shows it while the unit's own transport, checked above, stays stopped.
            let led = try await world.status().led
            XCTAssertTrue(led.playing, "the play LED shows the host's deck playing, whatever the unit's own transport (LedStatus.transport = \(led.transport))")
            // And it goes dark again when the host's deck stops: the unit's own state (stopped) is authoritative.
            bench.player.send(.pause)
            try await bench.run(ms: 200)
            XCTAssertEqual(bench.status.transport, .paused)
            let dark = try await world.status().led
            XCTAssertFalse(dark.playing, "the host's deck paused: the LED falls back to the unit's own (stopped) transport")
            try await expectEqual(try await world.status().transport, Int(TransportState.stopped.rawValue))
        }
    }

    func testTheSixtySecondStabilityRunKeepsEverythingInStep() async throws {
        try await withWorld { world in
            let c = world.client()
            var state = ControllerState(deck: .a)
            try await c.client.startStream(rateHz: 20)
            var rev = 0.0
            for i in 0..<60 {  // a minute of back-and-forth scratching with a moving fader
                let to = rev + (i % 2 == 0 ? 0.4 : -0.4)
                try await world.finger(rev: rev, pressure: 1)
                try await world.run(ms: 40)
                try await world.ramp("finger", from: rev, to: to, ms: 600)
                try await world.finger(pressure: 0)
                _ = try await world.command("fader u=0 raw=\(500 + (i * 53) % 3000)")
                try await world.run(ms: 360)
                rev = to
                state.apply(c.events.take())
            }
            let sim = try await world.status()
            XCTAssertEqual(state.jogTicks, sim.jogTotal)
            XCTAssertEqual(sim.int("outDropped"), 0, "the engine's output queue never overflowed")
            XCTAssertGreaterThan(c.telemetry.total, 900)
            let rejected = await c.client.rejectedFrameCount
            XCTAssertEqual(rejected, 0)
        }
    }
}

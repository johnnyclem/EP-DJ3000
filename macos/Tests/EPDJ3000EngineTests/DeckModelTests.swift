import Foundation
import XCTest
@testable import EPDJ3000Engine
import EPDJ3000Protocol

/// A deck, a player, and a clock that advances 2 ms per 96 rendered frames.
private final class DeckRig {
    let rig: Rig
    let deck: DeckModel
    var time = 0.0
    private var carry = 0.0

    init(deck id: Deck = .a, track: AudioBuffer = Sine.buffer(frequency: 440, seconds: 6)) {
        rig = Rig(track: track)
        deck = DeckModel(player: rig.core, deck: id)
    }

    var status: PlayerStatus { rig.status }
    var ch: UInt8 { deck.controller.channel }

    func send(_ events: MidiEvent...) { deck.handle(events, at: time) }

    func note(_ n: UInt8, _ down: Bool) -> MidiEvent {
        down ? .noteOn(channel: ch, note: n, velocity: 127) : .noteOff(channel: ch, note: n, velocity: 0)
    }

    func cc(_ n: UInt8, _ v: UInt8) -> MidiEvent { .controlChange(channel: ch, controller: n, value: v) }

    /// Renders `seconds` in 2 ms steps; `jogSpeed` (if given) feeds the jog stream the way the device would.
    @discardableResult
    func run(_ seconds: Double, jogSpeed: Double? = nil) -> [Float] {
        var out = [Float]()
        for _ in 0..<Int((seconds / 0.002).rounded()) {
            if let s = jogSpeed {
                carry += ScratchConstants.ticksPerSecond(forSpeed: s) * 0.002
                let whole = Int(carry)
                carry -= Double(whole)
                if whole != 0 { send(MidiEvent.jog(delta: whole, channel: ch)) }
            }
            out += rig.render(frames: 96)
            time += 0.002
        }
        return out
    }

    func set14(_ control: Control14, _ value: UInt16) {
        deck.handle(MidiEvent.cc14(control, channel: ch, value: value), at: time)
    }

    func tap() {
        send(note(Proto.Note.play, true))
        run(0.012)
        send(note(Proto.Note.play, false))
        run(0.1)
    }
}

final class DeckModelTests: XCTestCase {
    private let nominal = ScratchConstants.nominalTicksPerSecond

    // MARK: transport

    func testPlatterTapTogglesPlayAndPause() {
        let d = DeckRig()
        d.tap()
        XCTAssertEqual(d.status.transport, .playing)
        d.run(0.2)
        XCTAssertGreaterThan(d.status.playheadFrames, 5_000)
        d.tap()
        XCTAssertEqual(d.status.transport, .paused)
        XCTAssertEqual(d.deck.transport, .paused)
        d.tap()
        XCTAssertEqual(d.status.transport, .playing)
    }

    func testCueButtonSetsPreviewsAndReturns() {
        let d = DeckRig()
        d.deck.player.send(.seek(seconds: 1.0))
        d.run(0.05)

        d.send(d.note(Proto.Note.cue, true))
        d.run(0.1)
        XCTAssertTrue(d.status.previewing)
        XCTAssertEqual(d.status.cueFrames, 48_000)
        XCTAssertTrue(d.deck.controller.buttons.contains(.cue))
        XCTAssertGreaterThan(d.status.playheadFrames, 48_000 + 3_000)

        d.send(d.note(Proto.Note.cue, false))
        d.run(0.05)
        XCTAssertFalse(d.status.previewing)
        XCTAssertEqual(d.status.playheadFrames, 48_000)

        d.tap()  // play, then CUE brings it back and pauses
        d.run(0.3)
        d.send(d.note(Proto.Note.cue, true))
        d.run(0.05)
        d.send(d.note(Proto.Note.cue, false))
        d.run(0.05)
        XCTAssertEqual(d.status.transport, .paused)
        XCTAssertEqual(d.status.playheadFrames, 48_000)
    }

    func testRevButtonPlaysBackwardsWhileHeld() {
        let d = DeckRig()
        d.deck.player.send(.seek(seconds: 3.0))
        d.tap()
        d.run(0.3)
        XCTAssertEqual(d.status.speed, 1.0)

        d.send(d.note(Proto.Note.rev, true))
        d.run(0.2)
        XCTAssertEqual(d.status.speed, -1.0)
        let before = d.status.playheadFrames
        d.run(0.1)
        XCTAssertLessThan(d.status.playheadFrames, before - 4_000)

        d.send(d.note(Proto.Note.rev, false))
        d.run(0.2)
        XCTAssertEqual(d.status.speed, 1.0)
        XCTAssertEqual(d.status.transport, .playing)
    }

    func testTransportMessagesFromAMasterUnit() {
        let d = DeckRig()
        d.send(d.cc(Proto.CC.transport, 1))
        d.run(0.05)
        XCTAssertEqual(d.status.transport, .playing)
        d.send(d.cc(Proto.CC.transport, 2))
        d.run(0.05)
        XCTAssertEqual(d.status.transport, .paused)
        d.send(d.cc(Proto.CC.transport, 0))
        d.run(0.05)
        XCTAssertEqual(d.status.transport, .stopped)

        d.send(.start)
        d.run(0.1)
        XCTAssertEqual(d.status.transport, .playing)
        d.send(.start)  // already playing: not restarted
        d.run(0.1)
        XCTAssertGreaterThan(d.status.playheadFrames, 8_000)
        d.send(.stop)
        d.run(0.05)
        XCTAssertEqual(d.status.transport, .paused)
        let held = d.status.playheadFrames
        d.send(.continue)
        d.run(0.1)
        XCTAssertEqual(d.status.transport, .playing)
        XCTAssertGreaterThan(d.status.playheadFrames, held)

        d.send(.stop, .songPosition(beats: 0))  // stop + rewind
        d.run(0.05)
        XCTAssertEqual(d.status.transport, .stopped)
        XCTAssertEqual(d.status.playheadFrames, 0)

        d.send(.songPosition(beats: 64))  // no beat grid: not followed
        d.run(0.05)
        XCTAssertEqual(d.status.playheadFrames, 0)
    }

    func testATapOnASyncMasterUnitIsOneToggleNotTwo() {
        let d = DeckRig()
        // The unit's transport and its platter-tap pulse both report the same tap (docs/PROTOCOL.md section 2.3).
        d.send(.start)
        d.send(d.note(Proto.Note.play, true))
        d.run(0.012)
        d.send(d.note(Proto.Note.play, false))
        d.run(0.3)
        XCTAssertEqual(d.status.transport, .playing, "Start then pulse is one tap: playing, not toggled back")

        d.send(.stop, .songPosition(beats: 3))  // second tap: pause at the announced beat
        d.send(d.note(Proto.Note.play, true))
        d.run(0.012)
        d.send(d.note(Proto.Note.play, false))
        d.run(0.1)
        XCTAssertEqual(d.status.transport, .paused)

        d.send(.continue)  // third tap
        d.send(d.note(Proto.Note.play, true))
        d.run(0.012)
        d.send(d.note(Proto.Note.play, false))
        d.run(0.1)
        XCTAssertEqual(d.status.transport, .playing)
    }

    func testAPulseWellAfterATransportMessageIsANewTap() {
        let d = DeckRig()
        d.send(.start)
        d.run(DeckModel.pulseAfterTransportWindow * 2)
        d.send(d.note(Proto.Note.play, true))
        d.run(0.1)
        XCTAssertEqual(d.status.transport, .paused)

        d.send(d.cc(Proto.CC.transport, 1))  // the transport CC counts as a transport message too
        d.send(d.note(Proto.Note.play, true))
        d.run(0.1)
        XCTAssertEqual(d.status.transport, .playing)
    }

    // MARK: platter

    func testTouchHoldsTheTrackAndReleaseLetsItGo() {
        let d = DeckRig()
        d.tap()
        d.run(0.3)
        d.send(d.note(Proto.Note.touch, true))
        d.run(0.1)
        XCTAssertTrue(d.status.touched)
        XCTAssertEqual(d.status.speed, 0)
        let held = d.status.playheadFrames
        d.run(0.2)
        XCTAssertEqual(d.status.playheadFrames, held)

        d.send(d.note(Proto.Note.touch, false))
        d.run(0.3)
        XCTAssertFalse(d.status.touched)
        XCTAssertEqual(d.status.speed, 1.0)
    }

    func testJogMessagesScratchTheTrackForwardAndBack() {
        let d = DeckRig()
        d.deck.player.send(.seek(seconds: 2.0))
        d.run(0.05)
        d.send(d.note(Proto.Note.touch, true))
        d.run(0.05)

        d.run(0.3, jogSpeed: 2)  // clockwise at 2x
        XCTAssertEqual(d.status.speed, 2.0, accuracy: 0.25, "tick quantisation, ~10 %")
        let a = d.status.playheadFrames
        d.run(0.3, jogSpeed: 2)
        let forwardDelta = d.status.playheadFrames - a
        XCTAssertEqual(forwardDelta, 2 * 0.3 * 48_000, accuracy: 2 * 0.3 * 48_000 * 0.03, "3 % over 300 ms")

        d.run(0.3, jogSpeed: -2)
        let b = d.status.playheadFrames
        d.run(0.3, jogSpeed: -2)
        XCTAssertEqual(d.status.playheadFrames - b, -2 * 0.3 * 48_000, accuracy: 2 * 0.3 * 48_000 * 0.03)

        // Finger stops: the jog stream goes quiet. The track stops with it.
        d.run(0.08)
        XCTAssertEqual(d.status.speed, 0)
        let still = d.status.playheadFrames
        d.run(0.2)
        XCTAssertEqual(d.status.playheadFrames, still)
        XCTAssertEqual(d.deck.platterSpeed(at: d.time), 0)
    }

    func testJogBeforeTouchIsIgnored() {
        let d = DeckRig()
        d.deck.player.send(.seek(seconds: 2.0))
        d.run(0.05)
        d.run(0.2, jogSpeed: 3)  // free-spinning platter, not touched
        XCTAssertEqual(d.status.playheadFrames, 96_000)
        XCTAssertEqual(d.deck.controller.jogTicks, Int(3 * nominal * 0.2), accuracy: 3)
    }

    func testTouchStartsFromZeroVelocityNotTheLastGesture() {
        let d = DeckRig()
        d.deck.player.send(.seek(seconds: 2.0))
        d.run(0.05)
        d.send(d.note(Proto.Note.touch, true))
        d.run(0.2, jogSpeed: 4)
        d.send(d.note(Proto.Note.touch, false))
        d.run(0.1)
        d.send(d.note(Proto.Note.touch, true))  // lands again without moving
        d.run(0.1)
        XCTAssertEqual(d.status.speed, 0)
    }

    // MARK: fader and knob

    func testCrossfaderRoleCutsAndOpensTheOutput() {
        let d = DeckRig()
        d.set14(.crossfader, 16383)
        d.tap()
        d.run(0.3)
        XCTAssertEqual(d.run(0.05).map { abs($0) }.max()!, 0.8, accuracy: 0.01)

        d.set14(.crossfader, 0)
        d.run(0.05)
        XCTAssertTrue(d.run(0.05).allSatisfy { $0 == 0 })
        XCTAssertEqual(d.status.transport, .playing, "cutting does not stop the record")

        d.set14(.crossfader, 819)  // 5 % of travel = half-way up the sharp-cut knee
        d.run(0.05)
        XCTAssertEqual(d.run(0.05).map { abs($0) }.max()!, 0.4, accuracy: 0.02)
    }

    func testCutCurveIsSelectable() {
        let d = DeckRig()
        d.deck.cutCurve = .linear
        d.set14(.crossfader, 4096)  // 25 %
        d.run(0.01)
        d.tap()
        d.run(0.3)
        XCTAssertEqual(d.run(0.05).map { abs($0) }.max()!, 0.2, accuracy: 0.01)

        d.deck.cutCurve = .sharpCut  // takes effect on the current position without waiting for the fader
        d.run(0.1)
        XCTAssertEqual(d.run(0.05).map { abs($0) }.max()!, 0.8, accuracy: 0.01)
    }

    func testChannelVolumeIsTheDefaultRoleAndAlwaysALinearGain() {
        let d = DeckRig()
        d.tap()
        d.run(0.3)
        XCTAssertEqual(d.run(0.05).map { abs($0) }.max()!, 0.8, accuracy: 0.01, "open until the pot first moves")
        XCTAssertNil(d.deck.controller.faderRole)

        // CC 7 / 39 is what a unit sends with its default faderRole.
        for (value, expected) in [(UInt16(0), Float(0)), (4096, 0.2), (8192, 0.4), (16383, 0.8)] as [(UInt16, Float)] {
            d.set14(.channelVolume, value)
            d.run(0.05)
            XCTAssertEqual(d.run(0.05).map { abs($0) }.max()!, expected, accuracy: 0.01, "pot at \(value)")
        }
        XCTAssertEqual(d.deck.controller.faderRole, .channelVolume)

        d.deck.cutCurve = .sharpCut  // the crossfader knee must not leak into channel volume (25 % would be fully open)
        d.set14(.channelVolume, 4096)
        d.run(0.05)
        XCTAssertEqual(d.run(0.05).map { abs($0) }.max()!, 0.2, accuracy: 0.01)
    }

    func testPitchRoleSetsTheTempoWithinPlusMinusEightPercent() {
        let d = DeckRig()
        d.set14(.pitch, 16383)
        XCTAssertEqual(d.deck.tempoRatio, 1.08, accuracy: 1e-9)
        XCTAssertEqual(d.deck.pitchPercent, 8, accuracy: 1e-6)
        d.tap()
        d.run(0.3)
        XCTAssertEqual(d.status.speed, 1.08, accuracy: 1e-6)

        d.set14(.pitch, 0)
        XCTAssertEqual(d.deck.tempoRatio, 0.92, accuracy: 1e-9)
        d.set14(.pitch, 8192)
        XCTAssertEqual(d.deck.tempoRatio, 1.0)
        d.run(0.3)
        XCTAssertEqual(d.status.speed, 1.0)
        XCTAssertEqual(d.status.gain, 1, "a pitch fader never mutes the deck")

        d.deck.pitchRange = 0.16
        d.set14(.pitch, 16383)
        XCTAssertEqual(d.deck.tempoRatio, 1.16, accuracy: 1e-9)
    }

    func testSwitchingFaderRoleAwayFromPitchResetsTheTempo() {
        for role in [Control14.channelVolume, .crossfader] {
            let d = DeckRig()
            d.set14(.pitch, 16383)
            XCTAssertEqual(d.deck.tempoRatio, 1.08, accuracy: 1e-9)
            d.set14(role, 16383)
            XCTAssertEqual(d.deck.tempoRatio, 1.0, "\(role)")
        }
    }

    func testKnobSetsTheOutputVolume() {
        let d = DeckRig()
        d.tap()
        d.run(0.3)
        XCTAssertEqual(d.run(0.05).map { abs($0) }.max()!, 0.8, accuracy: 0.01, "full volume until the knob is touched")
        d.set14(.knob, 8192)
        d.run(0.05)
        XCTAssertEqual(d.run(0.05).map { abs($0) }.max()!, 0.4, accuracy: 0.01)
        d.set14(.knob, 0)
        d.run(0.05)
        XCTAssertTrue(d.run(0.05).allSatisfy { $0 == 0 })
    }

    // MARK: tempo

    func testBpmFromTheUnitSetsTheTempoWhenTheTrackBpmIsKnown() {
        let d = DeckRig()
        d.deck.trackBpm = 120
        XCTAssertEqual(d.deck.bpm, 120)
        d.deck.handle(MidiEvent.cc14(.bpm, channel: d.ch, value: 1296), at: 0)  // 129.6
        XCTAssertEqual(d.deck.requestedBpm, 129.6)
        XCTAssertEqual(d.deck.tempoRatio, 1.08, accuracy: 1e-9)
        XCTAssertEqual(d.deck.bpm!, 129.6, accuracy: 1e-9)
        XCTAssertEqual(d.deck.pitchPercent, 8, accuracy: 1e-6)

        d.send(d.cc(Proto.CC.bpmNudge, 64 - 12))  // 12 steps down = -1.2 BPM
        XCTAssertEqual(d.deck.requestedBpm!, 128.4, accuracy: 1e-9)
        XCTAssertEqual(d.deck.tempoRatio, 128.4 / 120, accuracy: 1e-9)
        d.tap()
        d.run(0.3)
        XCTAssertEqual(d.status.speed, 128.4 / 120, accuracy: 1e-5)
    }

    func testBpmWithoutATrackBpmIsOnlyDisplayed() {
        let d = DeckRig()
        XCTAssertNil(d.deck.bpm)
        d.deck.handle(MidiEvent.cc14(.bpm, channel: d.ch, value: 1285), at: 0)
        XCTAssertEqual(d.deck.bpm!, 128.5, accuracy: 1e-9)
        XCTAssertEqual(d.deck.tempoRatio, 1.0)
        d.deck.handle(MidiEvent.cc14(.bpm, channel: d.ch, value: 19), at: 0)  // 1.9 BPM is not a tempo
        XCTAssertEqual(d.deck.requestedBpm!, 128.5, accuracy: 1e-9)
    }

    func testUiTempoControlIsClamped() {
        let d = DeckRig()
        d.deck.setTempoRatio(5)
        XCTAssertEqual(d.deck.tempoRatio, 2)
        d.deck.setTempoRatio(0)
        XCTAssertEqual(d.deck.tempoRatio, 0.5)
        d.deck.setTempoRatio(.nan)
        XCTAssertEqual(d.deck.tempoRatio, 0.5)
        d.deck.trackBpm = 90
        d.deck.setTempoRatio(1.1)
        XCTAssertEqual(d.deck.bpm!, 99, accuracy: 1e-9)
    }

    // MARK: channels

    func testEventsForTheOtherDeckAreIgnored() {
        let a = DeckRig(deck: .a)
        a.deck.handle(.noteOn(channel: 1, note: Proto.Note.touch, velocity: 127), at: 0)
        a.deck.handle(.noteOn(channel: 1, note: Proto.Note.play, velocity: 127), at: 0)
        a.deck.handle(.controlChange(channel: 1, controller: Proto.CC.jog, value: 100), at: 0)
        a.run(0.05)
        XCTAssertFalse(a.status.touched)
        XCTAssertEqual(a.status.transport, .stopped)

        let b = DeckRig(deck: .b)
        XCTAssertEqual(b.ch, 1)
        b.tap()
        XCTAssertEqual(b.status.transport, .playing)
    }

    // MARK: MIDI back to the unit

    func testFeedbackReportsOnlyChanges() {
        let d = DeckRig()
        d.run(0.01)
        XCTAssertEqual(d.deck.feedback(), [
            .noteOff(channel: 0, note: Proto.Note.play, velocity: 0),
            .noteOn(channel: 0, note: Proto.Note.cue, velocity: 127),  // parked on the cue point at the start
        ])
        XCTAssertEqual(d.deck.feedback(), [])

        d.tap()
        d.run(0.1)
        XCTAssertEqual(d.deck.feedback(), [
            .noteOn(channel: 0, note: Proto.Note.play, velocity: 127),
            .noteOff(channel: 0, note: Proto.Note.cue, velocity: 0),
        ])
        XCTAssertEqual(d.deck.feedback(), [])

        d.send(d.note(Proto.Note.cue, true))  // back to cue and pause
        d.run(0.1)
        XCTAssertEqual(d.deck.feedback(), [
            .noteOff(channel: 0, note: Proto.Note.play, velocity: 0),
            .noteOn(channel: 0, note: Proto.Note.cue, velocity: 127),
        ])

        d.deck.resetFeedback()
        XCTAssertEqual(d.deck.feedback().count, 2, "after a reconnect everything is sent again")
    }

    func testFeedbackUsesTheDecksChannelAndSurvivesTheWire() {
        let d = DeckRig(deck: .b)
        d.run(0.01)
        let bytes = d.deck.feedback().flatMap { $0.bytes }
        XCTAssertEqual(bytes, [0x81, 39, 0, 0x91, 36, 127])
        var parser = MidiStreamParser()
        XCTAssertEqual(parser.feed(bytes), [.noteOff(channel: 1, note: 39, velocity: 0),
                                            .noteOn(channel: 1, note: 36, velocity: 127)])
    }

    func testFeedbackIsNeverHeldBackWhateverTheUnitIsDoing() {
        // The model has no notion of the unit's mode, and the unit takes the play note as LED feedback only (section 2.6),
        // so a unit in SYNC_MASTER (clock, Start/Stop, tempo, tap tempo) gets the same LED updates as any other.
        let d = DeckRig()
        d.run(0.01)
        XCTAssertEqual(d.deck.feedback().count, 2)

        d.send(.clock, .clock, .start)
        d.deck.handle(MidiEvent.cc14(.bpm, channel: d.ch, value: 1285), at: d.time)
        d.send(d.note(Proto.Note.tap, true), d.note(Proto.Note.tap, false))
        d.run(0.1)
        XCTAssertEqual(d.status.transport, .playing)
        XCTAssertEqual(d.deck.feedback(), [.noteOn(channel: 0, note: Proto.Note.play, velocity: 127),
                                           .noteOff(channel: 0, note: Proto.Note.cue, velocity: 0)])

        d.send(.stop, .songPosition(beats: 0))
        d.run(0.1)
        let back = d.deck.feedback()
        XCTAssertEqual(back, [.noteOff(channel: 0, note: Proto.Note.play, velocity: 0),
                              .noteOn(channel: 0, note: Proto.Note.cue, velocity: 127)])
        for event in back {
            // LEDs only: never a transport CC or a tempo, which are the only things that command a master.
            switch event {
            case let .noteOn(_, note, _), let .noteOff(_, note, _): XCTAssertTrue([Proto.Note.play, Proto.Note.cue].contains(note))
            default: XCTFail("feedback must be LED notes only, got \(event)")
            }
        }
    }

    func testPreviewLightsTheCueLedAwayFromTheCuePoint() {
        let d = DeckRig()
        d.deck.player.send(.seek(seconds: 1))
        d.run(0.05)
        XCTAssertEqual(d.deck.feedback(), [.noteOff(channel: 0, note: Proto.Note.play, velocity: 0),
                                           .noteOff(channel: 0, note: Proto.Note.cue, velocity: 0)])
        d.send(d.note(Proto.Note.cue, true))
        d.run(0.3)
        XCTAssertFalse(d.status.isAtCue, "the playhead has moved off the cue point")
        XCTAssertTrue(d.status.cueLit, "but a preview keeps the LED lit")
        XCTAssertEqual(d.deck.feedback(), [.noteOn(channel: 0, note: Proto.Note.cue, velocity: 127)])
        d.send(d.note(Proto.Note.cue, false))
        d.run(0.05)
        XCTAssertEqual(d.deck.feedback(), [], "snapped back to the cue: still lit, nothing to send")
    }

    // MARK: a recorded session, as bytes off the wire

    /// What a unit on channel 1 sends for: open the fader (default role), tap to play, let it run, touch the platter, scratch
    /// forwards and back, let go, hold REV, then CUE. Running status is used the way a real device does.
    private static let session: [(t: Double, bytes: [UInt8])] = {
        var s = [(Double, [UInt8])]()
        s.append((0.000, [0xB0, 7, 127, 39, 127]))                 // channel volume 16383, the default fader role (MSB, LSB in running status)
        s.append((0.010, [0xB0, 24, 0, 56, 0]))                    // knob 0 -- then
        s.append((0.020, [0xB0, 24, 127, 56, 127]))                // knob up to full
        s.append((0.100, [0x90, 39, 127]))                         // platter tap: play pulse
        s.append((0.112, [0x90, 39, 0]))
        s.append((1.000, [0x90, 38, 127]))                         // finger down
        // 0.4 s of scratching: +1 tick/ms forwards (= ~1.76x), then 0.4 s backwards at the same rate.
        for i in 0..<200 { s.append((1.010 + Double(i) * 0.002, [0xB0, 22, 64 + 2])) }
        for i in 0..<200 { s.append((1.410 + Double(i) * 0.002, [0xB0, 22, 64 - 2])) }
        s.append((1.900, [0x90, 38, 0]))                           // finger up
        s.append((2.500, [0x90, 37, 127]))                         // REV down
        s.append((2.800, [0x90, 37, 0]))                           // REV up
        s.append((3.000, [0x90, 36, 127]))                         // CUE: playing -> back to cue, pause
        s.append((3.100, [0x90, 36, 0]))
        return s
    }()

    func testRecordedSessionReplayedThroughTheParser() {
        let d = DeckRig(track: Sine.buffer(frequency: 440, seconds: 12))
        let session = Self.session
        var parser = MidiStreamParser()
        var checkpoints = [String: PlayerStatus]()
        var out = [Float]()
        var next = 0

        func advance(to t: Double) {
            while d.time < t - 1e-9 {
                while next < session.count && session[next].t <= d.time + 1e-9 {
                    d.deck.handle(parser.feed(session[next].bytes), at: session[next].t)
                    next += 1
                }
                out += d.rig.render(frames: 96)
                d.time += 0.002
            }
        }

        advance(to: 0.9)
        checkpoints["playing"] = d.status
        advance(to: 1.0 + 0.001)
        advance(to: 1.2)
        checkpoints["scratchFwd"] = d.status
        advance(to: 1.4)
        let turnaround = d.status.playheadFrames
        advance(to: 1.8)
        checkpoints["scratchBack"] = d.status
        advance(to: 2.4)
        checkpoints["released"] = d.status
        advance(to: 2.75)
        checkpoints["reversing"] = d.status
        advance(to: 2.95)
        checkpoints["forwardAgain"] = d.status
        advance(to: 3.2)
        checkpoints["cued"] = d.status

        XCTAssertEqual(next, session.count, "every recorded message was replayed")
        XCTAssertTrue(allFinite(out))
        XCTAssertEqual(d.deck.controller.faderRole, .channelVolume)
        XCTAssertEqual(d.deck.controller.knob, 16383)

        let playing = checkpoints["playing"]!
        XCTAssertEqual(playing.transport, .playing)
        XCTAssertEqual(playing.speed, 1.0)
        XCTAssertEqual(playing.playheadFrames, (0.9 - 0.1) * 48_000, accuracy: 0.05 * 48_000, "~0.8 s in, less the start ramp")

        let fwd = checkpoints["scratchFwd"]!
        XCTAssertTrue(fwd.touched)
        XCTAssertEqual(fwd.speed, (2.0 / 0.002) / nominal, accuracy: 0.2, "+2 ticks / 2 ms = 1.76x, give or take an event in the window")
        XCTAssertGreaterThan(fwd.playheadFrames, playing.playheadFrames, "forward scratch moved it on")

        let back = checkpoints["scratchBack"]!
        XCTAssertLessThan(back.speed, -1.5)
        XCTAssertLessThan(back.playheadFrames, turnaround, "and backwards took it back")
        // 200 events x 2 ticks each way: the platter returned to where it was. The track did too, to within the smoothing lag.
        XCTAssertEqual(d.deck.controller.jogTicks, 0, accuracy: 4)

        let released = checkpoints["released"]!
        XCTAssertFalse(released.touched)
        XCTAssertEqual(released.speed, 1.0, "eased back to normal after letting go")
        XCTAssertEqual(released.transport, .playing)

        XCTAssertEqual(checkpoints["reversing"]!.speed, -1.0)
        XCTAssertEqual(checkpoints["forwardAgain"]!.speed, 1.0)

        let cued = checkpoints["cued"]!
        XCTAssertEqual(cued.transport, .paused)
        XCTAssertEqual(cued.playheadFrames, 0, "no cue was set, so back to the start")
        XCTAssertEqual(cued.speed, 0)
    }
}

private func XCTAssertEqual(_ a: Int, _ b: Int, accuracy: Int, file: StaticString = #filePath, line: UInt = #line) {
    XCTAssertLessThanOrEqual(abs(a - b), accuracy, "\(a) vs \(b)", file: file, line: line)
}

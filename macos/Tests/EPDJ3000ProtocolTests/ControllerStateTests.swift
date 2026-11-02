import XCTest
@testable import EPDJ3000Protocol

final class ControllerStateTests: XCTestCase {
    private func cc(_ n: UInt8, _ v: UInt8, ch: UInt8 = 0) -> MidiEvent { .controlChange(channel: ch, controller: n, value: v) }
    private func note(_ n: UInt8, _ vel: UInt8, ch: UInt8 = 0) -> MidiEvent {
        vel == 0 ? .noteOff(channel: ch, note: n, velocity: 0) : .noteOn(channel: ch, note: n, velocity: vel)
    }

    // MARK: jog

    func testJogAccumulatesSignedTicksWithClockwisePositive() {
        var s = ControllerState(deck: .a)
        s.apply([cc(22, 64 + 5), cc(22, 64 - 2), cc(22, 64)])
        XCTAssertEqual(s.jogTicks, 3)
        XCTAssertEqual(s.lastJogDelta, 0, "64 = no motion")
        s.apply(cc(22, 127))
        XCTAssertEqual(s.lastJogDelta, 63)
        XCTAssertEqual(s.jogTicks, 66)
    }

    func testJogAngleWrapsAtOneRevolutionInBothDirections() {
        var s = ControllerState(channel: 0)
        for _ in 0..<16 { s.apply(MidiEvent.jog(delta: 63, channel: 0)) }  // 1008 ticks
        XCTAssertEqual(s.jogAngleRadians, 1008.0 / 1024.0 * 2 * .pi, accuracy: 1e-12)
        s.apply(MidiEvent.jog(delta: 16, channel: 0))
        XCTAssertEqual(s.jogTicks, 1024)
        XCTAssertEqual(s.jogAngleRadians, 0, accuracy: 1e-12)
        XCTAssertEqual(s.jogRevolutions, 1, accuracy: 1e-12)

        var ccw = ControllerState(channel: 0)
        for _ in 0..<4 { ccw.apply(MidiEvent.jog(delta: -63, channel: 0)) }  // -252 ticks
        XCTAssertEqual(ccw.jogTicks, -252)
        XCTAssertEqual(ccw.jogAngleRadians, (1024.0 - 252.0) / 1024.0 * 2 * .pi, accuracy: 1e-12)
        XCTAssertGreaterThanOrEqual(ccw.jogAngleRadians, 0)
        XCTAssertLessThan(ccw.jogAngleRadians, 2 * .pi)
    }

    func testQuarterTurnIsHalfPi() {
        var s = ControllerState(channel: 0)
        s.apply(MidiEvent.jog(delta: 63, channel: 0))
        for _ in 0..<3 { s.apply(MidiEvent.jog(delta: 63, channel: 0)) }
        s.apply(MidiEvent.jog(delta: 4, channel: 0))
        XCTAssertEqual(s.jogTicks, 256)
        XCTAssertEqual(s.jogAngleRadians, .pi / 2, accuracy: 1e-12)
    }

    // MARK: touch and buttons

    func testTouchFollowsNoteOnAndBothFormsOfNoteOff() {
        var s = ControllerState(channel: 0)
        s.apply(note(38, 127))
        XCTAssertTrue(s.touched)
        s.apply(MidiEvent.noteOff(channel: 0, note: 38, velocity: 0))
        XCTAssertFalse(s.touched)
        s.apply(note(38, 127))
        var parser = MidiStreamParser()
        s.apply(parser.feed([0x90, 38, 0]))  // Note On velocity 0 straight off the wire
        XCTAssertFalse(s.touched)
    }

    func testButtonsAreIndependent() {
        var s = ControllerState(channel: 0)
        s.apply([note(36, 127), note(37, 127)])
        XCTAssertEqual(s.buttons, [.cue, .rev])
        s.apply(note(36, 0))
        XCTAssertEqual(s.buttons, [.rev])
        s.apply(note(37, 0))
        XCTAssertEqual(s.buttons, [])
    }

    func testPlayPulsesAndTapsAreCountedOnNoteOnOnly() {
        var s = ControllerState(channel: 0)
        s.apply([note(39, 127), note(39, 0), note(39, 127), note(39, 0), note(41, 127), note(41, 0)])
        XCTAssertEqual(s.playPulseCount, 2)
        XCTAssertEqual(s.tapCount, 1)
        XCTAssertEqual(s.buttons, [])
    }

    // MARK: 14-bit controls

    func testFaderMsbThenLsbAsTheDeviceSendsIt() {
        var s = ControllerState(channel: 0)
        s.apply(MidiEvent.cc14(.crossfader, channel: 0, value: 12345))
        XCTAssertEqual(s.fader, 12345)
        XCTAssertEqual(s.faderRole, .crossfader)
        XCTAssertEqual(s.faderNormalized, 12345.0 / 16383.0, accuracy: 1e-12)
    }

    func testFaderLsbBeforeMsbEndsAtTheSameValue() {
        var s = ControllerState(channel: 0)
        s.apply([cc(55, 5), cc(23, 10)])
        XCTAssertEqual(s.fader, (10 << 7) | 5)
    }

    func testFaderWithSkippedHalfKeepsTheOtherHalf() {
        var s = ControllerState(channel: 0)
        s.apply(MidiEvent.cc14(.crossfader, channel: 0, value: (50 << 7) | 99))
        s.apply(cc(23, 51))  // LSB unchanged, so the device skips it
        XCTAssertEqual(s.fader, (51 << 7) | 99)
        s.apply(cc(55, 1))  // MSB unchanged
        XCTAssertEqual(s.fader, (51 << 7) | 1)
    }

    func testFaderRoleFollowsTheControllerPairInUse() {
        var s = ControllerState(channel: 0)
        XCTAssertNil(s.faderRole)
        s.apply(MidiEvent.cc14(.channelVolume, channel: 0, value: 16383))
        XCTAssertEqual(s.faderRole, .channelVolume)
        XCTAssertEqual(s.fader, 16383)
        s.apply(MidiEvent.cc14(.pitch, channel: 0, value: 8192))
        XCTAssertEqual(s.faderRole, .pitch)
        XCTAssertEqual(s.fader, 8192)
    }

    func testKnobIsSeparateFromTheFader() {
        var s = ControllerState(channel: 0)
        s.apply(MidiEvent.cc14(.knob, channel: 0, value: 4000))
        XCTAssertEqual(s.knob, 4000)
        XCTAssertEqual(s.fader, 0)
        XCTAssertNil(s.faderRole)
    }

    // MARK: tempo

    func testBpmFromFourteenBitControl() {
        var s = ControllerState(channel: 0)
        XCTAssertNil(s.bpm)
        s.apply(MidiEvent.cc14(.bpm, channel: 0, value: 1285))
        XCTAssertEqual(s.bpmX10, 1285)
        XCTAssertEqual(s.bpm, 128.5)
    }

    func testBpmOutsideTheValidRangeIsNotATempo() {
        var s = ControllerState(channel: 0)
        s.apply(MidiEvent.cc14(.bpm, channel: 0, value: 1200))
        s.apply(MidiEvent.cc14(.bpm, channel: 0, value: 100))
        XCTAssertEqual(s.bpmX10, 1200)
        s.apply(MidiEvent.cc14(.bpm, channel: 0, value: 16383))
        XCTAssertEqual(s.bpmX10, 1200)
    }

    func testBpmNudgeMovesTenthsAndClamps() {
        var s = ControllerState(channel: 0)
        s.apply(cc(21, 64 + 5))
        XCTAssertNil(s.bpmX10, "a nudge needs a known tempo")
        s.apply(MidiEvent.cc14(.bpm, channel: 0, value: 1200))
        s.apply(cc(21, 64 + 5))
        XCTAssertEqual(s.bpmX10, 1205)
        s.apply(cc(21, 64 - 10))
        XCTAssertEqual(s.bpmX10, 1195)
        s.apply(MidiEvent.cc14(.bpm, channel: 0, value: 3000))
        s.apply(cc(21, 127))
        XCTAssertEqual(s.bpmX10, 3000)
        s.apply(MidiEvent.cc14(.bpm, channel: 0, value: 200))
        s.apply(cc(21, 1))
        XCTAssertEqual(s.bpmX10, 200)
    }

    // MARK: transport

    func testTransportCcIsAbsolute() {
        var s = ControllerState(channel: 0)
        XCTAssertEqual(s.transport, .stopped)
        s.apply(cc(29, 1))
        XCTAssertEqual(s.transport, .playing)
        s.apply(cc(29, 2))
        XCTAssertEqual(s.transport, .paused)
        s.apply(cc(29, 0))
        XCTAssertEqual(s.transport, .stopped)
        s.apply(cc(29, 77))
        XCTAssertEqual(s.transport, .stopped, "unknown value ignored")
    }

    func testStartStopContinueAndStopRewind() {
        var s = ControllerState(channel: 0)
        s.apply(.clock)
        XCTAssertEqual(s.positionClocks, 0, "clock while stopped does not move the position")

        s.apply(.start)
        XCTAssertEqual(s.transport, .playing)
        for _ in 0..<30 { s.apply(.clock) }
        XCTAssertEqual(s.positionClocks, 30)
        XCTAssertEqual(s.beats, 30.0 / 24.0, accuracy: 1e-12)

        s.apply([.stop, .songPosition(beats: 5)])  // pause: Stop then SPP at the current position
        XCTAssertEqual(s.transport, .paused)
        XCTAssertEqual(s.positionClocks, 30)

        s.apply(.continue)
        XCTAssertEqual(s.transport, .playing)
        s.apply(.clock)
        XCTAssertEqual(s.positionClocks, 31)

        s.apply([.stop, .songPosition(beats: 0)])  // stop + rewind
        XCTAssertEqual(s.transport, .stopped)
        XCTAssertEqual(s.positionClocks, 0)

        s.apply(.start)
        XCTAssertEqual(s.positionClocks, 0)
        XCTAssertEqual(s.transport, .playing)
    }

    func testSongPositionAloneDoesNotChangeAPlayingTransport() {
        var s = ControllerState(channel: 0)
        s.apply([.start, .songPosition(beats: 0)])
        XCTAssertEqual(s.transport, .playing)
    }

    // MARK: time code

    func testTimecodeFromQuarterFramesAndFullFrame() {
        let m = VectorFile.shared.mtc[0]
        var s = ControllerState(channel: 0)
        var parser = MidiStreamParser()
        for qf in m.quarterFrames.dropLast() { s.apply(parser.feed(qf)) }
        XCTAssertNil(s.timecode, "incomplete until piece 7")
        s.apply(parser.feed(m.quarterFrames.last!))
        XCTAssertEqual(s.timecode, Timecode(hours: 1, minutes: 2, seconds: 3, frames: 4, rate: .fps30))

        let m2 = VectorFile.shared.mtc[1]
        s.apply(parser.feed(m2.fullFrame))
        XCTAssertEqual(s.timecode, Timecode(hours: 23, minutes: 59, seconds: 58, frames: 24, rate: .fps25))
    }

    func testForeignSysExLeavesStateAlone() {
        var s = ControllerState(channel: 0)
        let before = s
        s.apply(.sysex(VectorFile.shared.frame("getInfo deck A").frame))
        XCTAssertEqual(s, before)
    }

    // MARK: addressing

    func testChannelMessagesForOtherChannelsAreIgnoredButRealtimeIsNot() {
        var b = ControllerState(deck: .b)
        XCTAssertEqual(b.channel, 1)
        b.apply([cc(22, 70, ch: 0), note(38, 127, ch: 0), note(36, 127, ch: 2), cc(29, 1, ch: 0)])
        XCTAssertEqual(b.jogTicks, 0)
        XCTAssertFalse(b.touched)
        XCTAssertEqual(b.buttons, [])
        XCTAssertEqual(b.transport, .stopped)

        b.apply([cc(22, 70, ch: 1), note(38, 127, ch: 1), .start])
        XCTAssertEqual(b.jogTicks, 6)
        XCTAssertTrue(b.touched)
        XCTAssertEqual(b.transport, .playing)
    }

    func testFourteenBitStateIsPerChannelSoDecksDoNotMixHalves() {
        var a = ControllerState(deck: .a)
        // Deck B's MSB on the shared stream must not combine with deck A's LSB.
        a.apply([cc(23, 10, ch: 1), cc(55, 5, ch: 0), cc(23, 3, ch: 0)])
        XCTAssertEqual(a.fader, (3 << 7) | 5)
    }

    func testEqualityLetsAUiDiffCheaply() {
        var a = ControllerState(channel: 0)
        let b = a
        XCTAssertEqual(a, b)
        a.apply(note(38, 127))
        XCTAssertNotEqual(a, b)
    }
}

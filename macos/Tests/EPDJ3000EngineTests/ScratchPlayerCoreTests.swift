import Foundation
import XCTest
@testable import EPDJ3000Engine
import EPDJ3000Protocol

private let nominalTicks = ScratchConstants.nominalTicksPerSecond

final class ScratchPlayerCoreTests: XCTestCase {

    // MARK: plain playback

    func testPlaysASineAtOriginalPitchOneFrameAtATime() {
        let rig = Rig(track: Sine.buffer(frequency: 1000, seconds: 2))
        rig.block = 311  // odd, to prove the block size does not matter
        rig.core.send(.play)
        rig.render(seconds: 0.1)  // let the start ramp settle
        let before = rig.status.playheadFrames

        let out = rig.render(frames: 24_000)
        let after = rig.status.playheadFrames

        XCTAssertEqual(out.count, 24_000)
        XCTAssertEqual(rig.lastRight.count, 24_000)
        XCTAssertEqual(after - before, 24_000, accuracy: 1e-6, "exactly one track frame per output frame")
        XCTAssertEqual(measureFrequency(out, sampleRate: 48_000), 1000, accuracy: 0.5)
        XCTAssertEqual(out.map { abs($0) }.max()!, 0.8, accuracy: 0.005)
        // Cubic Hermite reproduces the sine at the exact playhead position to 3e-5 here; linear would be off by 1.7e-3.
        for i in stride(from: 0, to: out.count, by: 97) {
            let expected = 0.8 * sin(2 * .pi * 1000 * (before + Double(i)) / 48_000)
            XCTAssertEqual(Double(out[i]), expected, accuracy: 1e-4)
        }
        // And between the samples, where the interpolator actually has to work (a 0.3-frame offset).
        let off = Rig(track: Sine.buffer(frequency: 1000, seconds: 2, sampleRate: 44_100), outputRate: 48_000)
        off.core.send(.play)
        off.render(seconds: 0.1)
        let p0 = off.status.playheadFrames
        let resampled = off.render(frames: 4_800)
        for i in stride(from: 0, to: resampled.count, by: 31) {
            let expected = 0.8 * sin(2 * .pi * 1000 * (p0 + Double(i) * 44_100.0 / 48_000.0) / 44_100)
            XCTAssertEqual(Double(resampled[i]), expected, accuracy: 1e-4)
        }
        XCTAssertEqual(rig.status.transport, .playing)
        XCTAssertTrue(allFinite(out))
    }

    func testTrackAtAnotherSampleRateKeepsItsPitch() {
        let rig = Rig(track: Sine.buffer(frequency: 1000, seconds: 2, sampleRate: 44_100), outputRate: 48_000)
        rig.core.send(.play)
        rig.render(seconds: 0.1)
        let before = rig.status.playheadFrames
        let out = rig.render(frames: 24_000)
        XCTAssertEqual(rig.status.playheadFrames - before, 24_000 * 44_100.0 / 48_000.0, accuracy: 1e-6)
        XCTAssertEqual(measureFrequency(out, sampleRate: 48_000), 1000, accuracy: 0.5)
    }

    func testTempoRatioScalesPitchAndAdvance() {
        let rig = Rig(track: Sine.buffer(frequency: 1000, seconds: 2))
        rig.core.update { $0.tempoRatio = 1.08 }
        rig.core.send(.play)
        rig.render(seconds: 0.2)
        let before = rig.status.playheadFrames
        let out = rig.render(frames: 24_000)
        XCTAssertEqual(rig.status.playheadFrames - before, 24_000 * Double(Float(1.08)), accuracy: 1e-6)
        XCTAssertEqual(measureFrequency(out, sampleRate: 48_000), 1000 * Double(Float(1.08)), accuracy: 0.5)

        rig.core.update { $0.tempoRatio = 0.92 }
        rig.render(seconds: 0.2)
        let b2 = rig.status.playheadFrames
        rig.render(frames: 24_000)
        XCTAssertEqual(rig.status.playheadFrames - b2, 24_000 * Double(Float(0.92)), accuracy: 1e-6)
    }

    func testInterleavedRenderingMatchesPlanar() {
        let track = Sine.buffer(frequency: 523, seconds: 1)
        let planar = ScratchPlayerCore(sampleRate: 48_000)
        let inter = ScratchPlayerCore(sampleRate: 48_000)
        planar.load(track)
        inter.load(track)
        var l = [Float](repeating: 0, count: 480), r = l
        var lr = [Float](repeating: 0, count: 960)
        for block in 0..<20 {
            if block == 1 { planar.send(.play); inter.send(.play) }
            if block == 10 { planar.setTouched(true); inter.setTouched(true) }
            planar.render(left: &l, right: &r, frameCount: 480)
            inter.renderInterleaved(&lr, frameCount: 480)
            for i in 0..<480 {
                XCTAssertEqual(lr[2 * i], l[i])
                XCTAssertEqual(lr[2 * i + 1], r[i])
            }
        }
    }

    func testNoTrackIsSilentAndIgnoresCommands() {
        let rig = Rig(track: nil)
        rig.core.send(.play)
        rig.core.send(.cueDown)
        let out = rig.render(seconds: 0.1)
        XCTAssertEqual(out.count, 4800)
        XCTAssertTrue(out.allSatisfy { $0 == 0 })
        XCTAssertFalse(rig.status.hasTrack)
        XCTAssertEqual(rig.status.transport, .stopped)
    }

    func testLoadingANewTrackResetsTheDeck() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 2))
        rig.playFrom(1.0)
        rig.core.send(.setCueHere)
        rig.render(seconds: 0.05)
        XCTAssertGreaterThan(rig.status.cueFrames, 40_000)

        rig.core.load(Sine.buffer(frequency: 880, seconds: 0.5))
        rig.render(seconds: 0.01)
        XCTAssertEqual(rig.status.transport, .stopped)
        XCTAssertEqual(rig.status.playheadFrames, 0)
        XCTAssertEqual(rig.status.cueFrames, 0)
        XCTAssertEqual(rig.status.trackFrames, 24_000)
        XCTAssertEqual(rig.status.speed, 0)
    }

    // MARK: scratching

    func testScratchForwardAt2xThenBackwardFollowsThePlatter() {
        let rig = Rig(track: Sine.buffer(frequency: 220, seconds: 4))
        rig.core.send(.seek(seconds: 1.0))
        rig.render(seconds: 0.05)
        XCTAssertEqual(rig.status.playheadFrames, 48_000, "the seek lands exactly")

        let twice = ScratchConstants.ticksPerSecond(forSpeed: 2)
        rig.core.setTouched(true)
        let fwd = rig.playheadTrace(seconds: 0.2, jog: twice)  // one entry per 96-frame block
        XCTAssertEqual(slope(fwd, from: 40, to: 99, perStep: 96), 2.0, accuracy: 0.01)
        XCTAssertTrue(zip(fwd, fwd.dropFirst()).allSatisfy { $1 > $0 }, "clockwise never moves the track backwards")
        let peak = fwd.last!

        let back = rig.playheadTrace(seconds: 0.2, jog: -twice)
        XCTAssertEqual(slope(back, from: 40, to: 99, perStep: 96), -2.0, accuracy: 0.01)
        XCTAssertLessThan(back.last!, peak - 10_000, "anticlockwise carries it back")
        XCTAssertGreaterThan(peak, 48_000 + 0.8 * 2 * 9_600, "about 2x for 200 ms, less the ramp")
    }

    func testReverseIsReversePlaybackWhileHeld() {
        let rig = Rig(track: Sine.buffer(frequency: 220, seconds: 4))
        rig.playFrom(2.0)
        rig.core.update { $0.reverseHeld = true }
        let rev = rig.playheadTrace(seconds: 0.3)
        XCTAssertEqual(slope(rev, from: 80, to: 149, perStep: 96), -1.0, accuracy: 1e-6)
        XCTAssertEqual(rig.status.speed, -1.0)
        XCTAssertEqual(rig.status.transport, .playing, "REV does not change the transport")

        rig.core.update { $0.reverseHeld = false }
        let fwd = rig.playheadTrace(seconds: 0.3)
        XCTAssertEqual(slope(fwd, from: 80, to: 149, perStep: 96), 1.0, accuracy: 1e-6)
    }

    func testReverseOutputIsTheTimeReversedTrack() {
        // A ramp makes direction obvious: forwards rises, backwards falls.
        let ramp = (0..<96_000).map { Float($0) / 96_000 }
        let rig = Rig(track: AudioBuffer(mono: ramp, sampleRate: 48_000))
        rig.playFrom(1.0)
        rig.core.update { $0.reverseHeld = true }
        rig.render(seconds: 0.2)
        let out = rig.render(seconds: 0.1)
        XCTAssertTrue(zip(out, out.dropFirst()).allSatisfy { $1 < $0 })
    }

    func testTouchingAndHoldingStillStopsTheTrack() {
        let rig = Rig(track: Sine.buffer(frequency: 220, seconds: 4))
        rig.playFrom(1.0)
        XCTAssertEqual(rig.status.speed, 1.0)

        rig.core.setTouched(true)  // finger down, not moving
        let brake = rig.speedTrace(seconds: 0.05)
        XCTAssertLessThan(abs(brake.last!), 1e-3)

        let heldFrom = rig.status.playheadFrames
        let out = rig.render(seconds: 0.3)
        XCTAssertEqual(rig.status.playheadFrames, heldFrom, "no drift while held")
        XCTAssertEqual(rig.status.speed, 0)
        XCTAssertEqual(out.min()!, out.max()!, "a stopped record is silence-of-motion: constant output")
        XCTAssertEqual(rig.status.transport, .playing, "touching does not change the transport")
    }

    func testReleaseEasesBackToTheNominalSpeedOverTheConfiguredTime() {
        let rig = Rig(track: Sine.buffer(frequency: 220, seconds: 8))
        rig.block = 32
        rig.playFrom(1.0)
        rig.core.setTouched(true)
        rig.render(seconds: 0.1)
        XCTAssertEqual(rig.status.speed, 0)

        rig.core.setTouched(false)
        let trace = rig.speedTrace(seconds: 0.3)  // one entry per 32 frames = 0.667 ms
        func speed(atMs ms: Double) -> Double { trace[Int(ms / 1000 * 48_000 / 32) - 1] }

        XCTAssertLessThan(speed(atMs: 1), 0.01, "not a step")
        XCTAssertLessThan(speed(atMs: 10), 0.2)
        XCTAssertTrue(zip(trace, trace.dropFirst()).allSatisfy { $1 >= $0 }, "monotonic: no overshoot")
        XCTAssertGreaterThan(speed(atMs: 120), 0.985, "default 120 ms")
        XCTAssertLessThan(speed(atMs: 120), 1.0)
        XCTAssertEqual(speed(atMs: 250), 1.0, "and then exactly nominal")

        // Slower on request.
        rig.core.setTouched(true)
        rig.render(seconds: 0.1)
        rig.core.update { $0.releaseEaseSeconds = 0.5 }
        rig.core.setTouched(false)
        let slow = rig.speedTrace(seconds: 0.6)
        XCTAssertEqual(slow[Int(0.12 * 48_000 / 32) - 1], 0.47, accuracy: 0.1)
        XCTAssertGreaterThan(slow[Int(0.5 * 48_000 / 32) - 1], 0.985)
    }

    func testReleasedWhilePausedStopsQuicklyInsteadOfEasing() {
        let rig = Rig(track: Sine.buffer(frequency: 220, seconds: 4))
        rig.playFrom(1.0)
        rig.core.send(.pause)
        rig.render(seconds: 0.2)
        rig.core.setTouched(true)
        rig.render(seconds: 0.1, jog: 2 * nominalTicks)  // scrub
        XCTAssertEqual(rig.status.speed, 2.0, accuracy: 1e-6)
        rig.core.setTouched(false)
        rig.render(seconds: 0.08)
        XCTAssertEqual(rig.status.speed, 0, "no long coast on a paused deck")
    }

    func testJogVelocityThatStopsBeingRefreshedFadesToZero() {
        let rig = Rig(track: Sine.buffer(frequency: 220, seconds: 4))
        rig.core.send(.seek(seconds: 2.0))
        rig.render(seconds: 0.05)
        rig.core.setTouched(true)
        rig.render(seconds: 0.1, jog: nominalTicks)
        XCTAssertEqual(rig.status.speed, 1.0, accuracy: 1e-6)

        // The refreshes stop: a finger that stops moving sends nothing at all.
        let fade = rig.speedTrace(seconds: 0.1)  // 96-frame blocks
        let atMs = { (ms: Double) in fade[Int(ms / 2) - 1] }
        XCTAssertGreaterThan(atMs(2), 0.8, "held for the block that was fresh")
        XCTAssertTrue(zip(fade, fade.dropFirst()).allSatisfy { $1 <= $0 })
        XCTAssertLessThan(atMs(40), 0.05)
        XCTAssertEqual(atMs(60), 0, accuracy: 0.002)
        XCTAssertEqual(fade.last!, 0)
    }

    func testJogTouchFromStoppedScrubsTheTrack() {
        // Scratching works on a deck that is not playing: touch and move scrubs, let go and it stays.
        let rig = Rig(track: Sine.buffer(frequency: 220, seconds: 4))
        rig.core.send(.seek(seconds: 1.0))
        rig.render(seconds: 0.05)
        rig.core.setTouched(true)
        rig.render(seconds: 0.1, jog: nominalTicks)
        let scrubbed = rig.status.playheadFrames
        XCTAssertGreaterThan(scrubbed, 48_000 + 3_000)
        rig.core.setTouched(false)
        rig.render(seconds: 0.2)
        XCTAssertEqual(rig.status.speed, 0)
        XCTAssertEqual(rig.status.transport, .stopped)
    }

    // MARK: fader

    func testFaderCutIsDeepAndFastButNotAClick() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 4))
        rig.playFrom(1.0)
        var out = rig.render(seconds: 0.05)
        XCTAssertEqual(out.map { abs($0) }.max()!, 0.8, accuracy: 0.01)

        rig.core.update { $0.faderPosition = 0 }  // cut (SHARP_CUT is the default curve)
        out += rig.render(seconds: 0.05)
        XCTAssertTrue(out.suffix(480).allSatisfy { $0 == 0 }, "fully cut within 50 ms")

        rig.core.update { $0.faderPosition = 0.05 }  // half-way up the sharp-cut knee
        out += rig.render(seconds: 0.05)
        XCTAssertEqual(out.suffix(480).map { abs($0) }.max()!, 0.4, accuracy: 0.01)

        rig.core.update { $0.faderPosition = 0.5 }  // sharp cut is fully open from 10 %
        out += rig.render(seconds: 0.05)
        XCTAssertEqual(out.suffix(480).map { abs($0) }.max()!, 0.8, accuracy: 0.01)

        // Gain slews at most 1.4 % of full scale per sample, on top of the sine's own slope.
        let sineStep = 0.8 * 2 * Float.pi * 440 / 48_000
        XCTAssertLessThan(maxStep(out), sineStep + 0.8 * 0.0138 + 0.005)
    }

    func testFaderCurves() {
        XCTAssertEqual(FaderCurve.linear.gain(at: 0.3), 0.3, accuracy: 1e-6)
        XCTAssertEqual(FaderCurve.sharpCut.gain(at: 0), 0)
        XCTAssertEqual(FaderCurve.sharpCut.gain(at: 0.05), 0.5, accuracy: 0.001)
        XCTAssertEqual(FaderCurve.sharpCut.gain(at: 0.1), 1, accuracy: 1e-3)
        XCTAssertEqual(FaderCurve.sharpCut.gain(at: 0.7), 1)
        XCTAssertEqual(FaderCurve.smooth.gain(at: 0.5), 0.5, accuracy: 1e-6)
        XCTAssertEqual(FaderCurve.smooth.gain(at: 0.25), 0.15625, accuracy: 1e-6)
        for curve in FaderCurve.allCases {
            XCTAssertEqual(curve.gain(at: 0), 0)
            XCTAssertEqual(curve.gain(at: 1), 1)
            XCTAssertEqual(curve.gain(at: -3), 0)
            XCTAssertEqual(curve.gain(at: 9), 1)
            XCTAssertEqual(curve.gain(at: .nan), 0)
            var last: Float = 0
            for k in 0...200 {
                let g = curve.gain(at: Float(k) / 200)
                XCTAssertGreaterThanOrEqual(g, last, "\(curve) must be monotonic")
                last = g
            }
        }
    }

    func testVolumeKnobScalesTheOutput() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 2))
        rig.core.update { $0.volume = 0.5 }
        rig.playFrom(0.5)
        let out = rig.render(seconds: 0.1)
        XCTAssertEqual(out.map { abs($0) }.max()!, 0.4, accuracy: 0.005)
    }

    // MARK: click-freedom

    func testClickDetectorSeesAStep() {
        // The detector the next test relies on must be able to fail.
        var x = Sine.samples(frequency: 440, seconds: 0.1)
        XCTAssertLessThan(maxStep(x), 0.06)
        x.replaceSubrange(1000..<1100, with: Sine.samples(frequency: 440, seconds: 0.1)[2000..<2100])
        XCTAssertGreaterThan(maxStep(x), 0.2)
    }

    func testEveryTransitionIsClickFree() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 6))
        let three = 3 * nominalTicks
        var out = [Float]()
        func play(_ s: Double, jog: Double? = nil) { out += rig.render(seconds: s, jog: jog) }

        rig.core.send(.play); play(0.3)                         // start ramp
        rig.core.send(.setCueHere); play(0.1)
        rig.core.setTouched(true); play(0.1)                    // brake onto the finger
        play(0.1, jog: three)                                   // scratch forwards at 3x
        play(0.1, jog: -three)                                  // reverse the platter through zero
        play(0.05, jog: 0.5 * nominalTicks)
        rig.core.setTouched(false); play(0.3)                   // 120 ms ease back
        rig.core.update { $0.reverseHeld = true }; play(0.2)    // REV
        rig.core.update { $0.reverseHeld = false }; play(0.2)
        rig.core.send(.pause); play(0.1)                        // brake
        rig.core.send(.play); play(0.2)
        rig.core.update { $0.tempoRatio = 1.08 }; play(0.1)     // pitch fader
        rig.core.send(.cueDown); play(0.1)                      // playing: back to cue (a jump)
        rig.core.send(.cueUp); play(0.05)
        rig.core.send(.cueDown); play(0.1)                      // paused: preview
        rig.core.send(.cueUp); play(0.1)                        // and snap back (a jump)
        rig.core.send(.seek(seconds: 3.3)); play(0.05)          // seek (a jump)
        rig.core.send(.play); play(0.2)
        rig.core.send(.stop); play(0.1)                         // stop = brake + jump to cue
        rig.core.send(.play); play(0.1)
        rig.core.update { $0.faderPosition = 0 }; play(0.05)    // cut and
        rig.core.update { $0.faderPosition = 1 }; play(0.05)    // reopen
        rig.core.send(.seek(seconds: 5.99)); play(0.05)         // jump right to the end
        play(0.3)                                               // and play out (end of track)

        let sineStepAt3x = 0.8 * 2 * Float.pi * 440 / 48_000 * 3
        XCTAssertTrue(allFinite(out))
        XCTAssertLessThan(maxStep(out), sineStepAt3x + 0.01)
        XCTAssertEqual(rig.status.endCount, 1)
    }

    // MARK: track edges

    func testPlayingOffTheEndStopsAtTheCueWithoutLooping() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 0.1))  // 4800 frames
        rig.block = 128
        rig.core.send(.play)
        var endedAt: Int?
        let out = rig.render(seconds: 0.3) { done in
            if endedAt == nil && rig.status.endCount == 1 { endedAt = done }
        }
        XCTAssertNotNil(endedAt)
        XCTAssertEqual(rig.status.transport, .stopped)
        XCTAssertEqual(rig.status.endCount, 1, "ran out once, did not loop")
        XCTAssertEqual(rig.status.playheadFrames, rig.status.cueFrames)
        XCTAssertEqual(rig.status.speed, 0)
        // Everything after the de-click fade (144 frames) is exact silence.
        XCTAssertTrue(out[(endedAt! + 144 + 128)...].allSatisfy { $0 == 0 })
        XCTAssertTrue(allFinite(out))
        XCTAssertGreaterThan(out[0..<4000].map { abs($0) }.max()!, 0.7, "it did play")

        rig.core.send(.play)
        rig.render(seconds: 0.3)
        XCTAssertEqual(rig.status.endCount, 2, "and plays again from the cue")
    }

    func testPlayingOffTheEndReturnsToACueThatWasSet() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 0.5))
        rig.core.send(.seek(seconds: 0.2))
        rig.render(seconds: 0.01)
        rig.core.send(.setCueHere)
        rig.core.send(.play)
        rig.render(seconds: 0.6)
        XCTAssertEqual(rig.status.transport, .stopped)
        XCTAssertEqual(rig.status.playheadFrames, 0.2 * 48_000, accuracy: 1e-6)
    }

    func testScratchingPastTheEndPinsWithoutStoppingTheTransport() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 0.5))
        rig.playFrom(0.3)
        rig.core.setTouched(true)
        let out = rig.render(seconds: 0.3, jog: 4 * nominalTicks)
        XCTAssertEqual(rig.status.playheadFrames, 23_999)
        XCTAssertEqual(rig.status.transport, .playing)
        XCTAssertEqual(rig.status.endCount, 0)
        XCTAssertEqual(rig.status.speed, 0)
        let tail = out.suffix(2400)
        XCTAssertEqual(tail.min()!, tail.max()!, "pinned = constant, not a click")
        XCTAssertTrue(allFinite(out))

        rig.core.setTouched(false)  // let go at the end of the track: now it runs out
        rig.render(seconds: 0.3)
        XCTAssertEqual(rig.status.endCount, 1)
        XCTAssertEqual(rig.status.transport, .stopped)
    }

    func testScratchingBackPastTheStartPinsAtFrameZero() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 1))
        rig.core.send(.seek(seconds: 0.02))
        rig.render(seconds: 0.05)
        rig.core.setTouched(true)
        let out = rig.render(seconds: 0.2, jog: -6 * nominalTicks)
        XCTAssertEqual(rig.status.playheadFrames, 0)
        XCTAssertEqual(rig.status.speed, 0)
        XCTAssertTrue(allFinite(out))

        rig.core.setTouched(false)
        rig.core.update { $0.reverseHeld = true }  // REV into the start of the track
        rig.render(seconds: 0.2)
        XCTAssertEqual(rig.status.playheadFrames, 0)
        XCTAssertEqual(rig.status.transport, .stopped, "reverse never stops the transport at the start")
    }

    func testTinyTrackIsHandled() {
        let rig = Rig(track: AudioBuffer(mono: [0.5, -0.5], sampleRate: 48_000))
        rig.core.send(.play)
        let out = rig.render(seconds: 0.05)
        XCTAssertTrue(allFinite(out))
        XCTAssertEqual(rig.status.endCount, 1)

        let one = Rig(track: AudioBuffer(mono: [0.5], sampleRate: 48_000))  // fewer than two frames = no track
        one.core.send(.play)
        XCTAssertTrue(one.render(seconds: 0.01).allSatisfy { $0 == 0 })
    }

    // MARK: cue

    func testCueSetsPreviewsWhileHeldAndSnapsBack() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 2))
        rig.core.send(.seek(seconds: 0.5))
        rig.render(seconds: 0.05)
        XCTAssertEqual(rig.status.playheadFrames, 24_000)
        XCTAssertFalse(rig.status.isAtCue, "the cue is still at 0")

        rig.core.send(.cueDown)  // paused away from the cue: set it here, and preview
        rig.render(seconds: 0.1)
        XCTAssertEqual(rig.status.cueFrames, 24_000)
        XCTAssertTrue(rig.status.previewing)
        XCTAssertTrue(rig.status.cueLit)
        XCTAssertEqual(rig.status.transport, .stopped, "a preview is not a transport change")
        XCTAssertGreaterThan(rig.status.playheadFrames, 24_000 + 3_000)

        rig.core.send(.cueUp)
        rig.render(seconds: 0.05)
        XCTAssertEqual(rig.status.playheadFrames, 24_000, "back on the cue, exactly")
        XCTAssertEqual(rig.status.speed, 0)
        XCTAssertFalse(rig.status.previewing)
        XCTAssertTrue(rig.status.isAtCue)

        rig.core.send(.cueDown)  // on the cue now: preview again without moving it
        rig.render(seconds: 0.05)
        XCTAssertEqual(rig.status.cueFrames, 24_000)
        rig.core.send(.cueUp)
        rig.render(seconds: 0.05)
        XCTAssertEqual(rig.status.playheadFrames, 24_000)
    }

    func testCueWhilePlayingJumpsBackAndPauses() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 3))
        rig.core.send(.seek(seconds: 0.5))
        rig.render(seconds: 0.01)
        rig.core.send(.setCueHere)
        rig.core.send(.play)
        rig.render(seconds: 0.5)
        XCTAssertGreaterThan(rig.status.playheadFrames, 24_000 + 20_000)

        rig.core.send(.cueDown)
        rig.render(seconds: 0.05)
        XCTAssertEqual(rig.status.transport, .paused)
        XCTAssertEqual(rig.status.playheadFrames, 24_000)
        XCTAssertEqual(rig.status.speed, 0)
        XCTAssertFalse(rig.status.previewing)
        rig.core.send(.cueUp)
        rig.render(seconds: 0.05)
        XCTAssertEqual(rig.status.playheadFrames, 24_000)
    }

    func testPlayWhileHoldingCueLatchesTheTransport() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 3))
        rig.core.send(.cueDown)
        rig.render(seconds: 0.05)
        XCTAssertTrue(rig.status.previewing)
        rig.core.send(.play)
        rig.render(seconds: 0.05)
        XCTAssertEqual(rig.status.transport, .playing)
        XCTAssertFalse(rig.status.previewing)

        rig.core.send(.cueUp)  // release: nothing to snap back to
        rig.render(seconds: 0.3)
        XCTAssertEqual(rig.status.transport, .playing)
        XCTAssertGreaterThan(rig.status.playheadFrames, 10_000)
    }

    func testStopBringsTheDeckBackToTheCue() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 3))
        rig.playFrom(1.0)
        let here = rig.status.playheadFrames  // commands run at the start of the next block, before it advances
        rig.core.send(.setCueHere)
        rig.render(seconds: 0.5)
        XCTAssertEqual(rig.status.cueFrames, here)
        XCTAssertGreaterThan(rig.status.playheadFrames, here + 20_000)
        rig.core.send(.stop)
        rig.render(seconds: 0.05)
        XCTAssertEqual(rig.status.transport, .stopped)
        XCTAssertEqual(rig.status.playheadFrames, here)
        XCTAssertEqual(rig.status.speed, 0)
    }

    func testPauseHoldsPositionAndPlayResumesFromIt() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 3))
        rig.playFrom(1.0)
        rig.core.send(.pause)
        rig.render(seconds: 0.2)
        let held = rig.status.playheadFrames
        XCTAssertEqual(rig.status.transport, .paused)
        rig.render(seconds: 0.2)
        XCTAssertEqual(rig.status.playheadFrames, held)

        rig.core.send(.togglePlay)
        rig.render(seconds: 0.2)
        XCTAssertEqual(rig.status.transport, .playing)
        XCTAssertGreaterThan(rig.status.playheadFrames, held + 5_000)
        rig.core.send(.togglePlay)
        rig.render(seconds: 0.1)
        XCTAssertEqual(rig.status.transport, .paused)
    }

    func testSeekClampsAndIgnoresGarbage() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 1))
        rig.core.send(.seek(seconds: 99))
        rig.render(seconds: 0.05)
        XCTAssertEqual(rig.status.playheadFrames, 47_999)
        rig.core.send(.seek(seconds: -5))
        rig.render(seconds: 0.05)
        XCTAssertEqual(rig.status.playheadFrames, 0)
        rig.core.send(.seek(seconds: 0.5))
        rig.core.send(.seek(seconds: .nan))
        rig.core.send(.seek(seconds: .infinity))
        rig.render(seconds: 0.05)
        XCTAssertEqual(rig.status.playheadFrames, 24_000, "non-finite seeks are dropped")
    }

    // MARK: constants

    func testJogToRateConstants() {
        XCTAssertEqual(Proto.jogTicksPerRev, 1024)
        XCTAssertEqual(ScratchConstants.nominalRpm, 33.3333333, accuracy: 1e-6)
        XCTAssertEqual(ScratchConstants.nominalRevolutionsPerSecond, 0.55556, accuracy: 1e-5)
        XCTAssertEqual(ScratchConstants.nominalTicksPerSecond, 568.889, accuracy: 1e-3)
        XCTAssertEqual(ScratchConstants.nominalTicksPerSecond, 569.0, accuracy: 0.2, "the spec's rounded figure")
        XCTAssertEqual(ScratchConstants.nominalTicksPerSecond, 1024 * 100.0 / 3.0 / 60.0, accuracy: 1e-9)

        // One 33 1/3 rpm revolution takes 1.8 s: 1024 ticks in 1.8 s is speed 1.0.
        XCTAssertEqual(ScratchConstants.speed(forTicksPerSecond: 1024 / 1.8), 1.0, accuracy: 1e-12)
        XCTAssertEqual(ScratchConstants.speed(forTicksPerSecond: 569), 1.0, accuracy: 0.001)
        XCTAssertEqual(ScratchConstants.speed(forTicksPerSecond: -2 * nominalTicks), -2.0, accuracy: 1e-12)
        XCTAssertEqual(ScratchConstants.ticksPerSecond(forSpeed: ScratchConstants.speed(forTicksPerSecond: 123.4)), 123.4,
                       accuracy: 1e-9)
    }

    func testPitchFaderMapsToPlusMinusEightPercentWithACentreDetent() {
        XCTAssertEqual(ScratchConstants.pitchRatio(faderNormalized: 0.5), 1)
        XCTAssertEqual(ScratchConstants.pitchRatio(faderNormalized: 8192.0 / 16383), 1, "centre of a 14-bit reading")
        XCTAssertEqual(ScratchConstants.pitchRatio(faderNormalized: 0.5 + 0.003), 1, "inside the detent")
        XCTAssertEqual(ScratchConstants.pitchRatio(faderNormalized: 0), 0.92, accuracy: 1e-12)
        XCTAssertEqual(ScratchConstants.pitchRatio(faderNormalized: 1), 1.08, accuracy: 1e-12)
        XCTAssertEqual(ScratchConstants.pitchRatio(faderNormalized: 1, range: 0.16), 1.16, accuracy: 1e-12)
        var last = 0.0
        for k in 0...1000 {
            let r = ScratchConstants.pitchRatio(faderNormalized: Double(k) / 1000)
            XCTAssertGreaterThanOrEqual(r, last)
            last = r
        }
    }

    // MARK: robustness

    func testNeverProducesNaNOrInfinityOrLeavesTheTrack() {
        let track = Sine.buffer(frequency: 1000, seconds: 0.7, amplitude: 0.9)
        let rig = Rig(track: track)
        var rng = XorShift(state: 0x9E37_79B9_7F4A_7C15)
        let bad: [Double] = [.nan, .infinity, -.infinity]

        for _ in 0..<4000 {
            rig.block = 1 + rng.int(700)
            switch rng.int(14) {
            case 0: rig.core.send(.play)
            case 1: rig.core.send(.pause)
            case 2: rig.core.send(.stop)
            case 3: rig.core.send(.togglePlay)
            case 4: rig.core.send(.cueDown)
            case 5: rig.core.send(.cueUp)
            case 6: rig.core.send(.setCueHere)
            case 7: rig.core.send(.seek(seconds: rng.int(8) == 0 ? bad[rng.int(3)] : rng.range(-3, 3)))
            case 8: rig.core.setTouched(rng.int(2) == 0)
            case 9: rig.core.setJog(ticksPerSecond: rng.int(8) == 0 ? bad[rng.int(3)] : rng.range(-90_000, 90_000))
            case 10: rig.core.update { $0.reverseHeld = rng.int(2) == 0 }
            case 11: rig.core.update {
                $0.tempoRatio = rng.int(8) == 0 ? Float(bad[rng.int(3)]) : Float(rng.range(-1, 10))
                $0.releaseEaseSeconds = rng.int(8) == 0 ? .nan : Float(rng.range(-1, 8))
            }
            case 12: rig.core.update {
                $0.faderPosition = rng.int(8) == 0 ? Float(bad[rng.int(3)]) : Float(rng.range(-1, 2))
                $0.volume = rng.int(8) == 0 ? Float(bad[rng.int(3)]) : Float(rng.range(-1, 2))
                $0.faderCurve = FaderCurve.allCases[rng.int(3)]
            }
            default: break
            }
            let out = rig.render(frames: rig.block)
            XCTAssertTrue(allFinite(out) && allFinite(rig.lastRight))
            XCTAssertLessThanOrEqual(out.map { abs($0) }.max()!, 1.0)
            let s = rig.status
            XCTAssertTrue(s.playheadFrames.isFinite && s.playheadFrames >= 0 && s.playheadFrames <= Double(track.frameCount - 1))
            XCTAssertTrue(s.speed.isFinite && abs(s.speed) <= ScratchConstants.maxSpeed)
            XCTAssertTrue(s.gain.isFinite && s.gain >= 0 && s.gain <= 1)
        }
    }

    func testSanitizeRejectsPoison() {
        var p = PlayerParameters()
        p.tempoRatio = .nan
        p.faderPosition = .infinity
        p.volume = -4
        p.jogTicksPerSecond = .nan
        p.releaseEaseSeconds = 0
        p.sanitize()
        XCTAssertEqual(p.tempoRatio, 1)
        XCTAssertEqual(p.faderPosition, 1)
        XCTAssertEqual(p.volume, 0)
        XCTAssertEqual(p.jogTicksPerSecond, 0)
        XCTAssertEqual(p.releaseEaseSeconds, 0.005)
    }

    func testAudioBufferScrubsNonFiniteSamples() {
        let b = AudioBuffer(left: [.nan, 1, .infinity, -.infinity], right: [0, .nan, 2, 3], sampleRate: 44_100)
        XCTAssertEqual(Array(UnsafeBufferPointer(start: b.left, count: 4)), [0, 1, 0, 0])
        XCTAssertEqual(Array(UnsafeBufferPointer(start: b.right, count: 4)), [0, 0, 2, 3])
        XCTAssertEqual(b.duration, 4.0 / 44_100)
    }
}

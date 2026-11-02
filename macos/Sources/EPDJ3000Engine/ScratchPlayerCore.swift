// Sample player whose speed follows the platter: reverse, touch-hold-and-scratch, CDJ-style cue, crossfader-style
// fader gain. Pure Swift (Foundation only), so it builds and tests on Linux as well as macOS.
//
// Threading: `render` runs on the audio thread and is allocation-free, lock-free-ish and exception-free (see
// ParameterMailbox). Everything else (`load`, `send`, `update`, `status`) is for the control thread and may be called
// from any thread.
//
// Signal path per output frame:
//   target speed -> two cascaded one-pole smoothers -> playhead += speed x (track rate / output rate)
//   -> cubic Hermite read at the fractional playhead -> x (smoothed fader gain) x (de-click ramp)
// Every discontinuity (cue return, seek, stop, end of track) goes through the de-click ramp: the gain ramps to zero,
// the playhead jumps while silent, then the gain ramps back. Everything else is continuous by construction.

import EPDJ3000Protocol
import Foundation

public struct ScratchPlayerConfig: Equatable, Sendable {
    /// A jog velocity that stops being refreshed fades linearly to zero over this long (see JogVelocityEstimator.window).
    public var jogFadeSeconds = JogVelocityEstimator.defaultWindow
    /// Time for the speed to settle (to within 1 %) after the platter's velocity changes or it is touched or released.
    public var jogSettleSeconds = 0.012
    /// Same for play, pause, REV and tempo changes.
    public var transportSettleSeconds = 0.030
    /// Fader gain smoothing. Short, so a cut is a cut.
    public var gainTimeConstantSeconds = 0.0015
    /// Fade out / in around a jump.
    public var declickSeconds = 0.003

    public init() {}
}

public final class ScratchPlayerCore: @unchecked Sendable {
    public let sampleRate: Double
    public let config: ScratchPlayerConfig

    let mailbox = ParameterMailbox()  // internal: tests hold its lock to exercise the contended path

    // Render-thread state. Only `renderBlock` and `handle` touch these.
    private var params = PlayerParameters()
    private var track: AudioBuffer?
    private var playhead = 0.0          // track frames
    private var cue = 0.0               // track frames
    private var transport = TransportState.stopped
    private var previewing = false
    private var speedStage = 0.0        // first smoother
    private var speed = 0.0             // second smoother = the speed actually used
    private var easing = false          // released and settling back onto the nominal speed
    private var wasTouched = false
    private var lastJogSerial: UInt32 = 0
    private var staleFrames = 0
    private var gain: Float = 1
    private var gainPrimed = false
    private var declick: Float = 1
    private var jumpPending = false
    private var jumpTarget = 0.0
    private var endCount: UInt32 = 0
    private let commandBuffer: UnsafeMutablePointer<PlayerCommand>

    private let poleJog: Double
    private let poleTransport: Double
    private let poleGain: Float
    private let declickStep: Float
    private let jogFadeFrames: Int

    public init(sampleRate: Double, config: ScratchPlayerConfig = ScratchPlayerConfig()) {
        precondition(sampleRate > 0 && sampleRate.isFinite, "sample rate must be positive")
        self.sampleRate = sampleRate
        self.config = config
        commandBuffer = .allocate(capacity: ParameterMailbox.commandCapacity)
        commandBuffer.initialize(repeating: .stop, count: ParameterMailbox.commandCapacity)
        poleJog = Self.pole(settleSeconds: config.jogSettleSeconds, sampleRate: sampleRate)
        poleTransport = Self.pole(settleSeconds: config.transportSettleSeconds, sampleRate: sampleRate)
        poleGain = Float(1 - exp(-1 / (max(config.gainTimeConstantSeconds, 1e-5) * sampleRate)))
        declickStep = Float(1 / max(1, config.declickSeconds * sampleRate))
        jogFadeFrames = max(1, Int(config.jogFadeSeconds * sampleRate))
    }

    deinit {
        commandBuffer.deallocate()
    }

    /// Per-sample coefficient of one of two cascaded one-poles that settle to 1 % after `settleSeconds`
    /// (critically damped: 1 % at 6.64 time constants). Two poles instead of one so a 500 Hz stream of quantised
    /// jog ticks leaves almost no ripple in the speed.
    private static func pole(settleSeconds: Double, sampleRate: Double) -> Double {
        let tau = max(settleSeconds, 1e-4) / 6.64
        return 1 - exp(-1 / (tau * sampleRate))
    }

    // MARK: control thread

    /// Replaces the track and returns the deck to stopped at frame 0 with the cue at 0. `nil` unloads.
    public func load(_ buffer: AudioBuffer?) {
        mailbox.load(buffer)
    }

    public func send(_ command: PlayerCommand) {
        mailbox.send(command)
    }

    /// Edit the parameter snapshot atomically.
    public func update(_ body: (inout PlayerParameters) -> Void) {
        mailbox.update(body)
    }

    public var parameters: PlayerParameters { mailbox.currentParameters() }

    public var status: PlayerStatus { mailbox.status() }

    /// Publish a fresh platter velocity (positive = clockwise = forwards). Call it every time the estimate is updated:
    /// the render side fades it out when the refreshes stop.
    public func setJog(ticksPerSecond: Double) {
        mailbox.update {
            $0.jogTicksPerSecond = Float(ticksPerSecond)
            $0.jogSerial &+= 1
        }
    }

    /// Touch-down starts from zero velocity ("held still") and touch-up discards what is left of the old one.
    public func setTouched(_ touched: Bool) {
        mailbox.update {
            $0.touched = touched
            $0.jogTicksPerSecond = 0
            $0.jogSerial &+= 1
        }
    }

    // MARK: render thread

    /// Planar stereo.
    public func render(left: UnsafeMutablePointer<Float>, right: UnsafeMutablePointer<Float>, frameCount: Int) {
        renderBlock(left, right, stride: 1, frameCount: frameCount)
    }

    /// Interleaved stereo (L R L R ...).
    public func renderInterleaved(_ out: UnsafeMutablePointer<Float>, frameCount: Int) {
        renderBlock(out, out + 1, stride: 2, frameCount: frameCount)
    }

    private func renderBlock(_ outL: UnsafeMutablePointer<Float>, _ outR: UnsafeMutablePointer<Float>,
                             stride: Int, frameCount: Int) {
        guard frameCount > 0 else { return }

        var jogFresh = false
        if let fetched = mailbox.fetch(parameters: &params, track: &track, commands: commandBuffer) {
            params.sanitize()
            if fetched.trackChanged { resetForNewTrack() }
            if params.jogSerial != lastJogSerial {
                lastJogSerial = params.jogSerial
                staleFrames = 0
                jogFresh = true
            }
            for i in 0..<fetched.commandCount { handle(commandBuffer[i]) }
        }
        if params.touched != wasTouched {
            wasTouched = params.touched
            easing = !params.touched
        }

        guard let track = track, track.frameCount >= 2 else {
            for n in 0..<frameCount {
                outL[n * stride] = 0
                outR[n * stride] = 0
            }
            mailbox.publish(PlayerStatus())
            return
        }

        let srcL = track.left
        let srcR = track.right
        let lastInt = track.frameCount - 1
        let lastIndex = Double(lastInt)
        let baseRatio = track.sampleRate / sampleRate
        let nominal = Double(params.tempoRatio)
        let reverse = params.reverseHeld
        let touched = params.touched
        let jogSpeed = Double(params.jogTicksPerSecond) / ScratchConstants.nominalTicksPerSecond
        let poleRelease = Self.pole(settleSeconds: Double(params.releaseEaseSeconds), sampleRate: sampleRate)
        let gainTarget = params.faderCurve.gain(at: params.faderPosition) * params.volume
        let maxSpeed = ScratchConstants.maxSpeed
        let fadeFrames = Double(jogFadeFrames)
        let poleJog = self.poleJog
        let poleTransport = self.poleTransport
        let poleGain = self.poleGain
        let declickStep = self.declickStep

        if !gainPrimed {
            gain = gainTarget
            gainPrimed = true
        }

        var playhead = self.playhead
        var speedStage = self.speedStage
        var speed = self.speed
        var easing = self.easing
        var staleFrames = self.staleFrames
        var gain = self.gain
        var declick = self.declick
        var jumpPending = self.jumpPending
        var jumpTarget = self.jumpTarget
        var transport = self.transport
        var previewing = self.previewing
        var endCount = self.endCount
        let cue = self.cue

        for n in 0..<frameCount {
            // 1. Where should the speed be heading?
            var target: Double
            var pole: Double
            if touched {
                // The platter has the record. Hold the velocity while it is being refreshed; fade it out once not.
                var fade = 1.0
                if !jogFresh {
                    fade = max(0, 1 - Double(staleFrames) / fadeFrames)
                    if staleFrames < jogFadeFrames { staleFrames += 1 }
                }
                target = min(maxSpeed, max(-maxSpeed, jogSpeed * fade))
                pole = poleJog
            } else {
                if reverse {
                    target = -nominal
                } else if transport == .playing || previewing {
                    target = nominal
                } else {
                    target = 0
                }
                // Long ease only for the "let go of a playing record" case; stopping uses the quick ramp.
                pole = (easing && target != 0) ? poleRelease : poleTransport
            }

            // 2. Smooth toward it, and snap once close so 0 is exactly 0 and nominal is exactly nominal.
            speedStage += pole * (target - speedStage)
            speed += pole * (speedStage - speed)
            if abs(target - speed) < 1e-4 && abs(target - speedStage) < 1e-4 {
                speed = target
                speedStage = target
                easing = false
            }

            // 3. De-click ramp and the jump it covers.
            if jumpPending {
                declick -= declickStep
                if declick <= 0 {
                    declick = 0
                    playhead = jumpTarget
                    speed = target  // silent now, so the speed may be set directly
                    speedStage = target
                    jumpPending = false
                }
            } else if declick < 1 {
                declick = min(1, declick + declickStep)
            }

            gain += poleGain * (gainTarget - gain)
            if abs(gainTarget - gain) < 1e-5 { gain = gainTarget }

            // 4. Read at the playhead (cubic Hermite, edges clamped).
            let i = Int(playhead)  // playhead is always in [0, lastIndex]
            let t = Float(playhead - Double(i))
            let i0 = i > 0 ? i - 1 : 0
            let i2 = i < lastInt ? i + 1 : lastInt
            let i3 = i + 2 <= lastInt ? i + 2 : lastInt
            let g = gain * declick
            outL[n * stride] = Self.hermite(srcL[i0], srcL[i], srcL[i2], srcL[i3], t) * g
            outR[n * stride] = Self.hermite(srcR[i0], srcR[i], srcR[i2], srcR[i3], t) * g

            // 5. Advance. The track edges stop the playhead; they do not wrap (no looping).
            let step = speed * baseRatio
            playhead += step
            if playhead >= lastIndex {
                playhead = lastIndex
                if step > 0 {
                    speed = 0
                    speedStage = 0
                    // Ran out while nobody holds the platter: the deck stops and returns to its cue point.
                    if !touched && (transport == .playing || previewing) {
                        endCount &+= 1
                        if transport == .playing { transport = .stopped }
                        previewing = false
                        jumpPending = true
                        jumpTarget = cue
                    }
                }
            } else if playhead <= 0 {
                playhead = 0
                if step < 0 {
                    speed = 0
                    speedStage = 0
                }
            }
        }

        self.playhead = playhead
        self.speedStage = speedStage
        self.speed = speed
        self.easing = easing
        self.staleFrames = staleFrames
        self.gain = gain
        self.declick = declick
        self.jumpPending = jumpPending
        self.jumpTarget = jumpTarget
        self.transport = transport
        self.previewing = previewing
        self.endCount = endCount

        var status = PlayerStatus()
        status.transport = transport
        status.playheadFrames = playhead
        status.trackFrames = track.frameCount
        status.trackSampleRate = track.sampleRate
        status.cueFrames = cue
        status.speed = speed
        status.previewing = previewing
        status.touched = touched
        status.endCount = endCount
        status.gain = gain
        mailbox.publish(status)
    }

    /// Catmull-Rom spline through x1 (t = 0) and x2 (t = 1), with x0 and x3 as outer neighbours.
    @inline(__always)
    private static func hermite(_ x0: Float, _ x1: Float, _ x2: Float, _ x3: Float, _ t: Float) -> Float {
        let c1 = 0.5 * (x2 - x0)
        let c2 = x0 - 2.5 * x1 + 2 * x2 - 0.5 * x3
        let c3 = 0.5 * (x3 - x0) + 1.5 * (x1 - x2)
        return ((c3 * t + c2) * t + c1) * t + x1
    }

    // MARK: commands (render thread, at the start of a block)

    private func resetForNewTrack() {
        playhead = 0
        cue = 0
        transport = .stopped
        previewing = false
        speedStage = 0
        speed = 0
        easing = false
        jumpPending = false
        declick = 1
    }

    /// Fade out, jump while silent, fade back in.
    private func scheduleJump(to frames: Double, lastIndex: Double) {
        guard frames.isFinite else { return }
        jumpTarget = min(lastIndex, max(0, frames))
        jumpPending = true
    }

    private func handle(_ command: PlayerCommand) {
        guard let track = track, track.frameCount >= 2 else { return }
        let lastIndex = Double(track.frameCount - 1)
        // Where the playhead is about to be, if a jump is already under way.
        let here = jumpPending ? jumpTarget : playhead
        switch command {
        case .play:
            if transport != .playing {
                transport = .playing
                previewing = false  // pressing PLAY while holding CUE latches the preview into real playback
                if here >= lastIndex - 1 { scheduleJump(to: cue, lastIndex: lastIndex) }
            }
        case .pause:
            previewing = false
            if transport == .playing { transport = .paused }
        case .stop:
            previewing = false
            transport = .stopped
            scheduleJump(to: cue, lastIndex: lastIndex)
        case .togglePlay:
            handle(transport == .playing ? .pause : .play)
        case .cueDown:
            if transport == .playing {
                transport = .paused
                previewing = false
                scheduleJump(to: cue, lastIndex: lastIndex)
            } else {
                if abs(here - cue) > track.sampleRate * 0.001 { cue = here }
                previewing = true
            }
        case .cueUp:
            if previewing {
                previewing = false
                scheduleJump(to: cue, lastIndex: lastIndex)
            }
        case .setCueHere:
            cue = here
        case let .seek(seconds):
            scheduleJump(to: seconds * track.sampleRate, lastIndex: lastIndex)
        }
    }
}

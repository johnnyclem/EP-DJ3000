// One deck as the app sees it: the MIDI a unit sends, folded into a ControllerState, plus the commands that
// MIDI gives a ScratchPlayerCore. No UI imports; a SwiftUI layer wraps it in an ObservableObject (or @Observable)
// and republishes after each `handle` call.
//
// Use from one thread (the one that receives MIDI). The player it commands is safe against the audio thread.
//
// Mapping (docs/PROTOCOL.md section 2):
//   touch (note 38)        platter touched: the track follows the platter, a still finger holds it
//   jog (CC 22)            platter velocity, via JogVelocityEstimator
//   cue (note 36)          CUE: playing -> back to cue and pause; paused -> set cue, preview while held
//   rev (note 37)          reverse while held
//   play (note 39)         platter-tap pulse: toggle play/pause
//   fader                  by faderRole: CROSSFADER -> gain through `cutCurve`; CHANNEL_VOLUME -> linear gain;
//                          PITCH -> tempo +-`pitchRange`
//   knob (CC 24)           output volume (with knobRole = BPM the device sends `bpm` instead and the knob is a tempo)
//   bpm (CC 20/52, 21)     requested tempo; with `trackBpm` known it sets the tempo ratio
//   transport CC 29, MIDI Start/Continue/Stop, SPP 0   follow a unit that is the transport master (SYNC_MASTER)

import EPDJ3000Protocol

public final class DeckModel {
    public let player: ScratchPlayerCore
    public private(set) var controller: ControllerState

    /// Curve for the CROSSFADER fader role. The firmware has its own `faderCurve`: leave one of the two LINEAR.
    public var cutCurve: FaderCurve = .sharpCut {
        didSet { applyFaderRole() }
    }

    /// Pitch fader span, e.g. 0.06 / 0.08 / 0.10 / 0.16.
    public var pitchRange = ScratchConstants.pitchRange {
        didSet { applyFaderRole() }
    }

    /// The loaded track's own tempo, if the app knows it (typed in or detected). Only used to turn tempo into BPM.
    public var trackBpm: Double?

    /// 1.0 = the track at its own tempo.
    public private(set) var tempoRatio = 1.0

    /// Tempo the unit last asked for (`bpm` / `bpmNudge`), in BPM.
    public private(set) var requestedBpm: Double?

    private var jog: JogVelocityEstimator
    private var lastPlayLed: Bool?
    private var lastCueLed: Bool?

    public init(player: ScratchPlayerCore, channel: UInt8) {
        self.player = player
        controller = ControllerState(channel: channel)
        jog = JogVelocityEstimator(window: player.config.jogFadeSeconds)
    }

    public convenience init(player: ScratchPlayerCore, deck: Deck) {
        self.init(player: player, channel: deck == .a ? Proto.defaultChannelDeckA : Proto.defaultChannelDeckB)
    }

    // MARK: state for the UI

    public var status: PlayerStatus { player.status }
    public var transport: TransportState { status.transport }
    public var pitchPercent: Double { (tempoRatio - 1) * 100 }

    /// What the deck is playing at: `trackBpm` x tempo when the track tempo is known, else what the unit last asked for.
    public var bpm: Double? {
        if let t = trackBpm { return t * tempoRatio }
        return requestedBpm
    }

    /// Platter velocity as the engine sees it, in playback speed (1.0 = 33 1/3 rpm).
    public func platterSpeed(at time: Double) -> Double { jog.speed(at: time) }

    /// For a tempo control in the UI. Clamped to 0.5...2.
    public func setTempoRatio(_ ratio: Double) {
        guard ratio.isFinite else { return }
        tempoRatio = min(2, max(0.5, ratio))
        let r = Float(tempoRatio)
        player.update { $0.tempoRatio = r }
    }

    // MARK: MIDI in

    /// `time` is seconds on a monotonic clock (CoreMIDI timestamps converted); it only matters for jog velocity.
    public func handle(_ event: MidiEvent, at time: Double) {
        if let channel = event.channel, channel != controller.channel { return }
        let before = Observed(controller)
        controller.apply(event)

        switch event {
        case let .controlChange(_, cc, value):
            if cc == Proto.CC.jog {
                jog.addTicks(MidiEncoding.decodeRelative(value), at: time)
                if controller.touched { player.setJog(ticksPerSecond: jog.velocity(at: time)) }
            } else if cc == Proto.CC.transport, let state = TransportState(rawValue: value) {
                follow(state)
            }
        case .start:
            // Start means "from the top". Ignored while already playing so our own LED feedback cannot make a
            // unit in SYNC_MASTER restart a track we were happy to be playing.
            if player.status.transport != .playing {
                player.send(.stop)
                player.send(.play)
            }
        case .continue:
            player.send(.play)
        case .stop:
            player.send(.pause)
        case let .songPosition(beats):
            // Stop + rewind is Stop then SPP 0. We have no beat grid, so any other position is not followed.
            if beats == 0 && controller.transport == .stopped { player.send(.stop) }
        default:
            break
        }

        if controller.touched != before.touched {
            if controller.touched { jog.reset() }
            player.setTouched(controller.touched)
        }
        if controller.buttons != before.buttons {
            let cueDown = controller.buttons.contains(.cue)
            if cueDown != before.buttons.contains(.cue) { player.send(cueDown ? .cueDown : .cueUp) }
            let revDown = controller.buttons.contains(.rev)
            if revDown != before.buttons.contains(.rev) { player.update { $0.reverseHeld = revDown } }
        }
        if controller.playPulseCount != before.playPulses {
            player.send(.togglePlay)
        }
        if controller.fader != before.fader || controller.faderRole != before.faderRole {
            if before.faderRole == .pitch && controller.faderRole != .pitch { setTempoRatio(1) }
            applyFaderRole()
        }
        if controller.knob != before.knob {
            let volume = Float(controller.knobNormalized)
            player.update { $0.volume = volume }
        }
        if controller.bpmX10 != before.bpmX10, let x10 = controller.bpmX10 {
            let requested = Double(x10) / 10
            requestedBpm = requested
            if let t = trackBpm, t > 0 { setTempoRatio(requested / t) }
        }
    }

    public func handle<S: Sequence>(_ events: S, at time: Double) where S.Element == MidiEvent {
        for e in events { handle(e, at: time) }
    }

    // MARK: MIDI out

    /// The note-ons/offs that bring the unit's CUE and PLAY LEDs in line with the player (docs/PROTOCOL.md: `cue` and
    /// `play` are "in" as well as "out"). Returns only what changed since the last call; call it after handling
    /// events and whenever the player's status may have changed (a UI timer is fine). The first call returns both.
    public func feedback() -> [MidiEvent] {
        let s = player.status
        let play = s.transport == .playing
        let cue = s.cueLit
        var out = [MidiEvent]()
        if lastPlayLed != play {
            out.append(Self.led(Proto.Note.play, play, channel: controller.channel))
            lastPlayLed = play
        }
        if lastCueLed != cue {
            out.append(Self.led(Proto.Note.cue, cue, channel: controller.channel))
            lastCueLed = cue
        }
        return out
    }

    /// Forget what the unit was told, so the next `feedback()` sends everything (after a reconnect).
    public func resetFeedback() {
        lastPlayLed = nil
        lastCueLed = nil
    }

    // MARK: internals

    private static func led(_ note: UInt8, _ on: Bool, channel: UInt8) -> MidiEvent {
        on ? .noteOn(channel: channel, note: note, velocity: 127) : .noteOff(channel: channel, note: note, velocity: 0)
    }

    private func follow(_ state: TransportState) {
        switch state {
        case .stopped: player.send(.stop)
        case .playing: player.send(.play)
        case .paused: player.send(.pause)
        }
    }

    /// Pushes whatever the slide pot currently means. Called when its value, its role, or the settings change.
    private func applyFaderRole() {
        guard let role = controller.faderRole else { return }
        let x = controller.faderNormalized
        switch role {
        case .crossfader:
            let (position, curve) = (Float(x), cutCurve)
            player.update { $0.faderPosition = position; $0.faderCurve = curve }
        case .channelVolume:
            let position = Float(x)
            player.update { $0.faderPosition = position; $0.faderCurve = .linear }
        case .pitch:
            player.update { $0.faderPosition = 1; $0.faderCurve = .linear }
            setTempoRatio(ScratchConstants.pitchRatio(faderNormalized: x, range: pitchRange))
        }
    }

    /// The few ControllerState fields `handle` compares across an event (cheaper than copying the whole struct,
    /// whose 14-bit assembler is a dictionary).
    private struct Observed {
        let touched: Bool
        let buttons: Buttons
        let fader: UInt16
        let faderRole: FaderRole?
        let knob: UInt16
        let bpmX10: Int?
        let playPulses: Int

        init(_ c: ControllerState) {
            touched = c.touched
            buttons = c.buttons
            fader = c.fader
            faderRole = c.faderRole
            knob = c.knob
            bpmX10 = c.bpmX10
            playPulses = c.playPulseCount
        }
    }
}

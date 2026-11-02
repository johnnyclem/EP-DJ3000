// Folds the MIDI stream a deck emits into one value (docs/PROTOCOL.md section 2).
// A value type so a UI layer can hold it in an @Observable / ObservableObject and diff it cheaply.

public struct ControllerState: Equatable, Sendable {
    /// Zero-based MIDI channel this state listens on. Channel messages for other channels are ignored;
    /// real-time, song position and time code are channel-less and always apply.
    public let channel: UInt8

    /// Platter position accumulated from jog messages. +1024 = one clockwise revolution (with jogInvert = 0).
    public private(set) var jogTicks = 0
    /// Delta of the most recent jog message; 0 once the platter reports no motion.
    public private(set) var lastJogDelta = 0
    public private(set) var touched = false
    /// 14-bit value of whichever slide-pot role (`faderRole`) the device is currently sending.
    public private(set) var fader: UInt16 = 0
    /// Which role the fader value belongs to; nil until the first fader message.
    public private(set) var faderRole: FaderRole?
    public private(set) var knob: UInt16 = 0
    public private(set) var buttons: Buttons = []
    /// Tempo x 10 from the `bpm` 14-bit control and `bpmNudge`; nil until the device reports one.
    public private(set) var bpmX10: Int?
    public private(set) var transport: TransportState = .stopped
    /// Position in MIDI clocks: reset by Start, advanced by each clock while playing, set by Song Position.
    public private(set) var positionClocks = 0
    public private(set) var timecode: Timecode?
    /// Counts `play` note-on pulses (platter tap). A pulse lasts ~10 ms, so a counter is the observable form.
    public private(set) var playPulseCount = 0
    /// Counts `tap` note-ons (tap-tempo detected in SYNC_MASTER mode).
    public private(set) var tapCount = 0

    private var cc14 = Cc14Assembler()
    private var mtc = MtcAssembler()

    public init(channel: UInt8) {
        self.channel = channel
    }

    public init(deck: Deck) {
        self.init(channel: deck == .a ? Proto.defaultChannelDeckA : Proto.defaultChannelDeckB)
    }

    public var jogRevolutions: Double { Double(jogTicks) / Double(Proto.jogTicksPerRev) }

    /// Clockwise angle in [0, 2 pi) relative to where the platter was when this state started counting.
    public var jogAngleRadians: Double {
        let wrapped = ((jogTicks % Proto.jogTicksPerRev) + Proto.jogTicksPerRev) % Proto.jogTicksPerRev
        return Double(wrapped) / Double(Proto.jogTicksPerRev) * 2 * Double.pi
    }

    public var faderNormalized: Double { Double(fader) / 16383 }
    public var knobNormalized: Double { Double(knob) / 16383 }
    public var bpm: Double? { bpmX10.map { Double($0) / 10 } }
    public var beats: Double { Double(positionClocks) / Double(Proto.clockPpqn) }

    public mutating func apply<S: Sequence>(_ events: S) where S.Element == MidiEvent {
        for e in events { apply(e) }
    }

    public mutating func apply(_ event: MidiEvent) {
        if let c = event.channel, c != channel { return }
        switch event {
        case let .noteOn(_, n, _): note(n, down: true)
        case let .noteOff(_, n, _): note(n, down: false)
        case let .controlChange(_, controller, value): controlChange(controller, value)
        case .start:
            transport = .playing
            positionClocks = 0
        case .continue:
            transport = .playing
        case .stop:
            transport = .paused
        case .clock:
            if transport == .playing { positionClocks += 1 }
        case let .songPosition(beats):
            positionClocks = Int(beats) * 6
            // Stop+rewind is Stop followed by SPP 0 (section 2.6).
            if beats == 0 && transport == .paused { transport = .stopped }
        case let .mtcQuarterFrame(piece, nibble):
            if let tc = mtc.feed(piece: piece, nibble: nibble) { timecode = tc }
        case let .sysex(bytes):
            if let tc = Timecode(fullFrame: bytes) { timecode = tc }
        case .other:
            break
        }
    }

    private mutating func note(_ note: UInt8, down: Bool) {
        switch note {
        case Proto.Note.cue: setButton(.cue, down)
        case Proto.Note.rev: setButton(.rev, down)
        case Proto.Note.touch: touched = down
        case Proto.Note.play: if down { playPulseCount += 1 }
        case Proto.Note.tap: if down { tapCount += 1 }
        default: break
        }
    }

    private mutating func setButton(_ button: Buttons, _ down: Bool) {
        if down { buttons.insert(button) } else { buttons.remove(button) }
    }

    private mutating func controlChange(_ controller: UInt8, _ value: UInt8) {
        switch controller {
        case Proto.CC.jog:
            lastJogDelta = MidiEncoding.decodeRelative(value)
            jogTicks += lastJogDelta
        case Proto.CC.bpmNudge:
            if let b = bpmX10 { bpmX10 = Self.clampBpm(b + MidiEncoding.decodeRelative(value)) }
        case Proto.CC.transport:
            if let t = TransportState(rawValue: value) { transport = t }
        default:
            guard let u = cc14.feed(channel: channel, controller: controller, value: value) else { return }
            switch u.control {
            case .crossfader: setFader(u.value, .crossfader)
            case .channelVolume: setFader(u.value, .channelVolume)
            case .pitch: setFader(u.value, .pitch)
            case .knob: knob = u.value
            case .bpm:
                // 20.0...300.0 is the valid range; anything else is not a tempo.
                if (Proto.bpmMinX10...Proto.bpmMaxX10).contains(Int(u.value)) { bpmX10 = Int(u.value) }
            }
        }
    }

    private mutating func setFader(_ value: UInt16, _ role: FaderRole) {
        fader = value
        faderRole = role
    }

    private static func clampBpm(_ v: Int) -> Int { min(Proto.bpmMaxX10, max(Proto.bpmMinX10, v)) }
}

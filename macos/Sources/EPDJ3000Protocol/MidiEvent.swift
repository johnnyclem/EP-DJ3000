// Typed MIDI events, their wire encodings, and a streaming parser (docs/PROTOCOL.md section 2).
//
// Channels are zero-based throughout (Deck A = 0, Deck B = 1).

public enum MidiEvent: Equatable, Sendable {
    case noteOff(channel: UInt8, note: UInt8, velocity: UInt8)
    /// The parser never produces velocity 0 here: Note On with velocity 0 arrives as `noteOff`.
    case noteOn(channel: UInt8, note: UInt8, velocity: UInt8)
    case controlChange(channel: UInt8, controller: UInt8, value: UInt8)
    case clock
    case start
    case `continue`
    case stop
    /// Song Position Pointer in MIDI beats (6 clocks each).
    case songPosition(beats: UInt16)
    case mtcQuarterFrame(piece: UInt8, nibble: UInt8)
    /// A complete `F0 ... F7` message, framing bytes included.
    case sysex([UInt8])
    /// Anything else, status byte included (program change, pitch bend, aftertouch, tune request, active sensing...).
    case other(status: UInt8, data: [UInt8])

    public var channel: UInt8? {
        switch self {
        case let .noteOff(c, _, _), let .noteOn(c, _, _), let .controlChange(c, _, _): return c
        case let .other(status, _) where status < 0xF0: return status & 0x0F
        default: return nil
        }
    }

    /// Wire bytes of this message. Channel is masked to 4 bits and data to 7 bits.
    public var bytes: [UInt8] {
        switch self {
        case let .noteOff(c, n, v): return [0x80 | (c & 0x0F), n & 0x7F, v & 0x7F]
        case let .noteOn(c, n, v): return [0x90 | (c & 0x0F), n & 0x7F, v & 0x7F]
        case let .controlChange(c, cc, v): return [0xB0 | (c & 0x0F), cc & 0x7F, v & 0x7F]
        case .clock: return [Proto.statusClock]
        case .start: return [Proto.statusStart]
        case .continue: return [Proto.statusContinue]
        case .stop: return [Proto.statusStop]
        case let .songPosition(beats): return [Proto.statusSongPosition, UInt8(beats & 0x7F), UInt8((beats >> 7) & 0x7F)]
        case let .mtcQuarterFrame(piece, nibble): return [Proto.statusMtcQuarterFrame, ((piece & 0x07) << 4) | (nibble & 0x0F)]
        case let .sysex(b): return b
        case let .other(status, data): return [status] + data
        }
    }
}

// MARK: - Value encodings

/// The 14-bit controls in the contract. MSB and LSB are always `msb + 32`, but the table is explicit.
public enum Control14: CaseIterable, Hashable, Sendable {
    case crossfader, channelVolume, pitch, knob, bpm

    public var msb: UInt8 {
        switch self {
        case .crossfader: return Proto.CC.crossfaderMsb
        case .channelVolume: return Proto.CC.channelVolumeMsb
        case .pitch: return Proto.CC.pitchMsb
        case .knob: return Proto.CC.knobMsb
        case .bpm: return Proto.CC.bpmMsb
        }
    }

    public var lsb: UInt8 {
        switch self {
        case .crossfader: return Proto.CC.crossfaderLsb
        case .channelVolume: return Proto.CC.channelVolumeLsb
        case .pitch: return Proto.CC.pitchLsb
        case .knob: return Proto.CC.knobLsb
        case .bpm: return Proto.CC.bpmLsb
        }
    }
}

public enum MidiEncoding {
    public static func split14(_ value: UInt16) -> (msb: UInt8, lsb: UInt8) {
        (UInt8((value >> 7) & 0x7F), UInt8(value & 0x7F))
    }

    public static func join14(msb: UInt8, lsb: UInt8) -> UInt16 {
        (UInt16(msb & 0x7F) << 7) | UInt16(lsb & 0x7F)
    }

    /// `64 + clamp(delta, -63, +63)`.
    public static func encodeRelative(_ delta: Int) -> UInt8 {
        UInt8(Proto.relativeCenter + min(63, max(-63, delta)))
    }

    /// Offset-64 decode. The device never sends 0 (it clamps to +-63), but if it arrives it is -64.
    public static func decodeRelative(_ value: UInt8) -> Int {
        Int(value & 0x7F) - Proto.relativeCenter
    }
}

public extension MidiEvent {
    /// MSB then LSB, as the device sends them.
    static func cc14(_ control: Control14, channel: UInt8, value: UInt16) -> [MidiEvent] {
        let (msb, lsb) = MidiEncoding.split14(value)
        return [.controlChange(channel: channel, controller: control.msb, value: msb),
                .controlChange(channel: channel, controller: control.lsb, value: lsb)]
    }

    static func jog(delta: Int, channel: UInt8) -> MidiEvent {
        .controlChange(channel: channel, controller: Proto.CC.jog, value: MidiEncoding.encodeRelative(delta))
    }
}

public enum MidiClock {
    /// Microseconds between 0xF8 ticks at 24 PPQN.
    public static func periodMicroseconds(bpmX10: Int) -> Double {
        60e6 / (Double(bpmX10) / 10.0 * Double(Proto.clockPpqn))
    }

    public static func bpmX10(periodMicroseconds: Double) -> Double {
        60e6 / (periodMicroseconds * Double(Proto.clockPpqn)) * 10.0
    }
}

// MARK: - 14-bit controls

/// Pairs MSB/LSB control changes into 14-bit values. Either half may arrive first or be skipped
/// (the device skips an unchanged half); a half never seen is taken as 0. Each arriving half yields
/// a value, so when both halves change the MSB-only value is reported momentarily before the final one.
public struct Cc14Assembler: Equatable, Sendable {
    public struct Update: Equatable, Sendable {
        public var channel: UInt8
        public var control: Control14
        public var value: UInt16
    }

    private struct Key: Hashable { var channel: UInt8; var control: Control14 }
    private struct Slot: Equatable { var msb: UInt8 = 0; var lsb: UInt8 = 0; var last: UInt16? }
    private var slots: [Key: Slot] = [:]

    public init() {}

    /// Returns an update when `controller` is a 14-bit half and the combined value changed.
    public mutating func feed(channel: UInt8, controller: UInt8, value: UInt8) -> Update? {
        guard let control = Control14.allCases.first(where: { $0.msb == controller || $0.lsb == controller }) else { return nil }
        let key = Key(channel: channel, control: control)
        var slot = slots[key] ?? Slot()
        if controller == control.msb { slot.msb = value & 0x7F } else { slot.lsb = value & 0x7F }
        let combined = MidiEncoding.join14(msb: slot.msb, lsb: slot.lsb)
        let changed = slot.last != combined
        slot.last = combined
        slots[key] = slot
        return changed ? Update(channel: channel, control: control, value: combined) : nil
    }
}

// MARK: - MIDI time code

public enum MtcFrameRate: UInt8, CaseIterable, Sendable {
    case fps24 = 0, fps25 = 1, fps2997Drop = 2, fps30 = 3

    public var framesPerSecond: Double {
        switch self {
        case .fps24: return 24
        case .fps25: return 25
        case .fps2997Drop: return 29.97
        case .fps30: return 30
        }
    }
}

public struct Timecode: Equatable, Sendable, CustomStringConvertible {
    public var hours: Int
    public var minutes: Int
    public var seconds: Int
    public var frames: Int
    public var rate: MtcFrameRate

    public init(hours: Int, minutes: Int, seconds: Int, frames: Int, rate: MtcFrameRate) {
        self.hours = hours
        self.minutes = minutes
        self.seconds = seconds
        self.frames = frames
        self.rate = rate
    }

    public var description: String {
        func two(_ v: Int) -> String { v < 10 ? "0\(v)" : "\(v)" }
        return "\(two(hours)):\(two(minutes)):\(two(seconds)):\(two(frames))"
    }

    /// Universal real-time full-frame message `F0 7F <dev> 01 01 hr mn sc fr F7`, `hr = fps << 5 | hours`.
    public init?(fullFrame bytes: [UInt8]) {
        guard bytes.count == 10, bytes[0] == 0xF0, bytes[1] == 0x7F, bytes[3] == 0x01, bytes[4] == 0x01, bytes[9] == 0xF7,
              let rate = MtcFrameRate(rawValue: (bytes[5] >> 5) & 0x03) else { return nil }
        self.init(hours: Int(bytes[5] & 0x1F), minutes: Int(bytes[6]), seconds: Int(bytes[7]), frames: Int(bytes[8]), rate: rate)
    }
}

/// Collects eight quarter-frame pieces (0...7, in order) into a `Timecode`. The result names the frame at which
/// piece 0 was sent; a receiver locking to it adds the two frames that the transmission took.
public struct MtcAssembler: Equatable, Sendable {
    private var nibbles = [UInt8](repeating: 0, count: 8)
    private var next = 0

    public init() {}

    public mutating func feed(piece: UInt8, nibble: UInt8) -> Timecode? {
        let p = Int(piece & 0x07)
        if p == 0 {
            next = 0
        } else if p != next {
            next = 0
            return nil
        }
        nibbles[p] = nibble & 0x0F
        next = p + 1
        guard p == 7 else { return nil }
        next = 0
        let n = nibbles.map(Int.init)
        guard let rate = MtcFrameRate(rawValue: UInt8((n[7] >> 1) & 0x03)) else { return nil }
        return Timecode(hours: n[6] | ((n[7] & 1) << 4),
                        minutes: n[4] | ((n[5] & 3) << 4),
                        seconds: n[2] | ((n[3] & 3) << 4),
                        frames: n[0] | ((n[1] & 1) << 4),
                        rate: rate)
    }
}

// MARK: - Streaming parser

/// Turns a raw MIDI byte stream (any chunking) into events. Handles running status, real-time bytes interleaved
/// anywhere (including inside SysEx), SysEx split across chunks, and aborts SysEx on a stray status byte.
public struct MidiStreamParser: Sendable {
    public let maxSysExLength: Int

    private var status: UInt8 = 0          // status of the message being assembled (0 = none)
    private var data: [UInt8] = []
    private var sysex: [UInt8] = []
    private var inSysEx = false
    private var sysexOverflow = false

    /// SysEx longer than `maxSysExLength` bytes is discarded (resynchronises at its F7).
    public init(maxSysExLength: Int = 4096) {
        self.maxSysExLength = maxSysExLength
    }

    public mutating func reset() {
        status = 0
        data.removeAll(keepingCapacity: true)
        sysex.removeAll(keepingCapacity: true)
        inSysEx = false
        sysexOverflow = false
    }

    public mutating func feed(_ bytes: [UInt8]) -> [MidiEvent] {
        var out = [MidiEvent]()
        for b in bytes { if let e = feed(b) { out.append(e) } }
        return out
    }

    public mutating func feed(_ byte: UInt8) -> MidiEvent? {
        if byte >= 0xF8 { return realtime(byte) }  // never disturbs a message in progress

        if byte >= 0x80 {
            if byte == 0xF7 {
                defer { reset() }
                guard inSysEx, !sysexOverflow else { return nil }
                return .sysex(sysex + [0xF7])
            }
            reset()
            if byte == 0xF0 {
                inSysEx = true
                sysex.append(0xF0)
                return nil
            }
            status = byte
            if Self.dataLength(status: byte) == 0 {
                status = 0
                return systemCommon(byte, [])
            }
            return nil
        }

        if inSysEx {
            if sysex.count < maxSysExLength { sysex.append(byte) } else { sysexOverflow = true }
            return nil
        }
        guard status != 0 else { return nil }  // data byte with no status to attach to
        data.append(byte)
        guard data.count == Self.dataLength(status: status) else { return nil }
        let complete = data
        data.removeAll(keepingCapacity: true)
        if status >= 0xF0 {
            let s = status
            status = 0  // system common cancels running status
            return systemCommon(s, complete)
        }
        return channelMessage(status, complete)  // running status stays armed
    }

    private static func dataLength(status: UInt8) -> Int {
        switch status {
        case 0xC0...0xDF: return 1
        case 0x80...0xBF, 0xE0...0xEF: return 2
        case 0xF1, 0xF3: return 1
        case 0xF2: return 2
        default: return 0  // F4, F5, F6
        }
    }

    private func realtime(_ byte: UInt8) -> MidiEvent? {
        switch byte {
        case Proto.statusClock: return .clock
        case Proto.statusStart: return .start
        case Proto.statusContinue: return .continue
        case Proto.statusStop: return .stop
        case 0xFE, 0xFF: return .other(status: byte, data: [])
        default: return nil  // 0xF9 and 0xFD are undefined
        }
    }

    private func systemCommon(_ status: UInt8, _ d: [UInt8]) -> MidiEvent? {
        switch status {
        case Proto.statusMtcQuarterFrame: return .mtcQuarterFrame(piece: (d[0] >> 4) & 0x07, nibble: d[0] & 0x0F)
        case Proto.statusSongPosition: return .songPosition(beats: MidiEncoding.join14(msb: d[1], lsb: d[0]))
        case 0xF3, 0xF6: return .other(status: status, data: d)
        default: return nil  // 0xF4, 0xF5 are undefined
        }
    }

    private func channelMessage(_ status: UInt8, _ d: [UInt8]) -> MidiEvent {
        let ch = status & 0x0F
        switch status & 0xF0 {
        case 0x80: return .noteOff(channel: ch, note: d[0], velocity: d[1])
        case 0x90: return d[1] == 0 ? .noteOff(channel: ch, note: d[0], velocity: 0) : .noteOn(channel: ch, note: d[0], velocity: d[1])
        case 0xB0: return .controlChange(channel: ch, controller: d[0], value: d[1])
        default: return .other(status: status, data: d)
        }
    }
}

// Human-readable MIDI for the monitor and SysEx log. Pure functions over the protocol library's types.

import EPDJ3000Protocol

public enum MidiCategory: String, CaseIterable, Identifiable, Sendable {
    case notes = "Notes"
    case controlChange = "CC"
    case jog = "Jog"
    case clock = "Clock"
    case transport = "Start/Stop"
    case timecode = "MTC"
    case sysex = "SysEx"
    case other = "Other"

    public var id: String { rawValue }
}

public enum MidiFormat {
    public static func category(of event: MidiEvent) -> MidiCategory {
        switch event {
        case .noteOn, .noteOff: return .notes
        case let .controlChange(_, controller, _): return controller == Proto.CC.jog ? .jog : .controlChange
        case .clock: return .clock
        case .start, .continue, .stop, .songPosition: return .transport
        case .mtcQuarterFrame: return .timecode
        case .sysex: return .sysex
        case .other: return .other
        }
    }

    public static func hex(_ bytes: [UInt8], limit: Int = 48) -> String {
        let shown = bytes.prefix(limit).map { byte -> String in
            let h = String(byte, radix: 16, uppercase: true)
            return h.count == 1 ? "0" + h : h
        }.joined(separator: " ")
        return bytes.count > limit ? shown + " ... (\(bytes.count) B)" : shown
    }

    /// EP-DJ3000 telemetry replies arrive at up to 100 Hz; they are counted, not logged.
    public static func isTelemetryFrame(_ sysex: [UInt8]) -> Bool {
        // F0 7D 45 44 ver deck cmd ...
        sysex.count > 7 && sysex[1] == Proto.SysEx.manufacturerId && sysex[2] == Proto.SysEx.modelId0
            && sysex[3] == Proto.SysEx.modelId1 && sysex[6] == SysExReply.telemetry.rawValue
    }

    // MARK: names

    public static func noteName(_ note: UInt8) -> String? {
        switch note {
        case Proto.Note.cue: return "CUE"
        case Proto.Note.rev: return "REV"
        case Proto.Note.touch: return "TOUCH"
        case Proto.Note.play: return "PLAY"
        case Proto.Note.tap: return "TAP"
        default: return nil
        }
    }

    public static func controllerName(_ cc: UInt8) -> String? {
        switch cc {
        case Proto.CC.jog: return "jog"
        case Proto.CC.bpmNudge: return "BPM nudge"
        case Proto.CC.transport: return "transport"
        default:
            for control in Control14.allCases {
                if cc == control.msb { return "\(name(of: control)) MSB" }
                if cc == control.lsb { return "\(name(of: control)) LSB" }
            }
            return nil
        }
    }

    private static func name(of control: Control14) -> String {
        switch control {
        case .crossfader: return "crossfader"
        case .channelVolume: return "channel volume"
        case .pitch: return "pitch"
        case .knob: return "knob"
        case .bpm: return "BPM"
        }
    }

    public static func commandName(_ command: SysExCmd) -> String {
        switch command {
        case .getInfo: return "GET_INFO"
        case .getConfig: return "GET_CONFIG"
        case .setConfig: return "SET_CONFIG"
        case .saveConfig: return "SAVE_CONFIG"
        case .loadDefaults: return "LOAD_DEFAULTS"
        case .stream: return "STREAM"
        case .calBegin: return "CAL_BEGIN"
        case .calEnd: return "CAL_END"
        case .setDeck: return "SET_DECK"
        case .reboot: return "REBOOT"
        case .linkStatus: return "LINK_STATUS"
        case .testTone: return "TEST_TONE"
        case .identify: return "IDENTIFY"
        }
    }

    public static func replyName(_ reply: SysExReply) -> String {
        switch reply {
        case .info: return "INFO"
        case .config: return "CONFIG"
        case .telemetry: return "TELEMETRY"
        case .calResult: return "CAL_RESULT"
        case .link: return "LINK"
        }
    }

    // MARK: description

    /// One line for a MIDI event, e.g. `Note On  ch1  36 CUE  vel 127`.
    public static func describe(_ event: MidiEvent) -> String {
        switch event {
        case let .noteOn(c, n, v):
            return "Note On  ch\(c + 1)  \(n)\(noteName(n).map { " " + $0 } ?? "")  vel \(v)"
        case let .noteOff(c, n, v):
            return "Note Off ch\(c + 1)  \(n)\(noteName(n).map { " " + $0 } ?? "")  vel \(v)"
        case let .controlChange(c, cc, v):
            let name = controllerName(cc).map { " " + $0 } ?? ""
            let value: String
            switch cc {
            case Proto.CC.jog, Proto.CC.bpmNudge:
                let d = MidiEncoding.decodeRelative(v)
                value = d >= 0 ? "+\(d)" : "\(d)"
            case Proto.CC.transport:
                value = TransportState(rawValue: v).map { "\($0)" } ?? "\(v)"
            default:
                value = "\(v)"
            }
            return "CC       ch\(c + 1)  \(cc)\(name)  \(value)"
        case .clock: return "Clock"
        case .start: return "Start"
        case .continue: return "Continue"
        case .stop: return "Stop"
        case let .songPosition(beats): return "Song Position  \(beats) beats"
        case let .mtcQuarterFrame(piece, nibble): return "MTC quarter frame  piece \(piece)  nibble \(nibble)"
        case let .sysex(bytes): return describeSysEx(bytes)
        case let .other(status, data): return "Status 0x\(hex([status]))  \(hex(data))"
        }
    }

    public static func describeSysEx(_ bytes: [UInt8]) -> String {
        if let tc = Timecode(fullFrame: bytes) { return "MTC full frame  \(tc)" }
        do {
            let frame = try SysExFrame.decode(bytes)
            return "EP-DJ3000 deck \(frame.deck == Proto.SysEx.deckAny ? "any" : String(frame.deck))  \(describe(frame))"
        } catch SysExFrameError.notEPDJ3000 {
            return "SysEx (other device)  \(bytes.count) B  \(hex(bytes, limit: 16))"
        } catch {
            return "SysEx (bad EP-DJ3000 frame: \(error))  \(hex(bytes, limit: 16))"
        }
    }

    private static func describe(_ frame: SysExFrame) -> String {
        switch frame.kind {
        case let .command(command):
            switch command {
            case .setConfig:
                if let (key, value) = configPair(frame.payload) { return "SET_CONFIG  \(key.name) = \(value)" }
            case .getConfig:
                if let first = frame.payload.first {
                    return "GET_CONFIG  " + (first == 0xFF ? "all" : ConfigKey(rawValue: first).map { ConfigTable.desc($0).name } ?? "key \(first)")
                }
            default: break
            }
            return commandName(command) + (frame.payload.isEmpty ? "" : "  " + hex(frame.payload, limit: 16))
        case let .reply(reply):
            if reply == .config, let (key, value) = configPair(frame.payload) { return "CONFIG  \(key.name) = \(value)" }
            return replyName(reply) + "  " + hex(frame.payload, limit: 16)
        case let .ack(command):
            return "ACK  " + (SysExCmd(rawValue: command).map(commandName) ?? "cmd 0x\(hex([command]))")
        case let .nak(command, code):
            let cmd = SysExCmd(rawValue: command).map(commandName) ?? "cmd 0x\(hex([command]))"
            let err = SysExError(rawValue: code).map { "\($0)" } ?? "error \(code)"
            return "NAK  \(cmd)  \(err)"
        case let .unknown(code):
            return "unknown code 0x\(hex([code]))  \(hex(frame.payload, limit: 16))"
        }
    }

    private static func configPair(_ payload: [UInt8]) -> (key: ConfigDesc, value: Int32)? {
        guard payload.count >= 5, let key = ConfigKey(rawValue: payload[0]) else { return nil }
        var u: UInt32 = 0
        for i in 0..<4 { u |= UInt32(payload[1 + i]) << UInt32(8 * i) }
        return (ConfigTable.desc(key), Int32(bitPattern: u))
    }
}

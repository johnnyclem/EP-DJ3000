// Error messages worded for the person at the bench rather than for a log.

import EPDJ3000Protocol
import Foundation

public enum ErrorText {
    public static func describe(_ error: Error) -> String {
        switch error {
        case let e as DeviceError: return describe(e)
        case let e as CalibrationError: return describe(e)
        case let e as SysExError: return describe(e)
        case is CancellationError: return "Cancelled."
        default: return error.localizedDescription   // LocalizedError text, or the system's wording for NSErrors
        }
    }

    public static func describe(_ error: DeviceError) -> String {
        switch error {
        case let .timeout(cmd):
            return "No reply to \(MidiFormat.commandName(cmd)). Is the firmware running and the cable the data one?"
        case let .nak(cmd, e):
            return "\(MidiFormat.commandName(cmd)) refused: \(describe(e))"
        case let .unknownNak(cmd, code):
            return "\(MidiFormat.commandName(cmd)) refused with unknown error code \(code)."
        case let .malformedReply(cmd):
            return "The reply to \(MidiFormat.commandName(cmd)) could not be read (firmware and app disagree about the format)."
        case let .protocolMismatch(info):
            return "The unit speaks protocol v\(info.protocolVersion); this app speaks v\(Proto.protocolVersion). Update one of them."
        case .transportClosed:
            return "The unit disconnected."
        case let .sendFailed(why):
            return "Could not send to the unit: \(why)"
        }
    }

    public static func describe(_ error: SysExError) -> String {
        switch error {
        case .unknownCmd: return "unknown command"
        case .badLength: return "wrong payload length"
        case .badChecksum: return "bad checksum"
        case .badKey: return "unknown setting"
        case .outOfRange: return "value out of range"
        case .busy: return "unit busy"
        case .notSupported: return "not supported by this hardware (for the test tone: no DAC fitted)"
        case .calNotActive: return "no calibration in progress"
        case .calFailed: return "calibration failed (not enough travel)"
        case .flashError: return "flash write failed"
        }
    }

    public static func describe(_ error: CalibrationError) -> String {
        switch error {
        case let .insufficientTravel(span, required):
            return "The control only moved \(span) counts; it needs at least \(required). Sweep it end to end."
        case let .unstableIdle(noise):
            return "The pads are not quiet (noise \(noise) per-mille). Take your hands off the platter and keep clear of it."
        case let .deadSector(sector):
            return "Sector \(sector + 1) reads zero: its wire is not connected."
        case let .noFingerDetected(sector):
            return "No touch seen on sector \(sector + 1). Check its wiring, or press harder and cover the whole sector."
        case let .insufficientContrast(weakest, required):
            return "The weakest sector rises only \(weakest) per-mille; at least \(required + 1) is needed. Improve that sector's contact or shielding."
        case let .insufficientMotion(ticks):
            return "The platter only moved \(ticks) ticks. Spin it a quarter turn or more."
        case .notComplete:
            return "The step is not finished yet."
        }
    }
}

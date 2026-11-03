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
            return "The weakest touch signal rises only \(weakest) per-mille; at least \(required + 1) is needed. Improve that pad's contact or shielding."
        case .padNotConnected:
            return "The hold pad reads zero: its wire is not connected, or the contact between the spinning platter and the pad is open."
        case .noHandDetected:
            return "No touch seen on the platter. Lay a flat hand on it and hold. If the reading still does not move, check the wire to the pad and the contact between the rotating platter and the pad."
        case let .insufficientMotion(ticks):
            return "The platter only moved \(ticks) ticks. Spin it a quarter turn or more."
        case .notComplete:
            return "The step is not finished yet."
        }
    }

    /// The encoder check's verdict worded as the next thing to do. The first sentence says what was found.
    public static func describe(_ verdict: EncoderDiagnosis.Verdict) -> String {
        let turn = EncoderDiagnosis.countsPerTurn
        switch verdict {
        case .noEncoder:
            return "No AS5600 answered when the unit started. Check the I2C wiring (SDA and SCL on the right pins, not swapped), "
                + "that the breakout is powered from 3.3 V (many have a 3.3 V / 5 V strap or jumper; the ESP32-S3's I2C is 3.3 V) "
                + "and has its pull-ups, then restart the unit."
        case let .readsFailing(samples):
            return "The AS5600 answered at start-up, but the unit got no valid angle in \(samples) readings. The I2C connection is "
                + "loose or intermittent (reseat SDA and SCL), or the platter source setting is not AS5600. Restart the unit after fixing it."
        case let .notMoving(span):
            let turns = String(format: "%.2f", Double(span) / Double(turn))
            return "The angle moved only \(span) of \(turn) counts (\(turns) turn). Spin the platter at least one full turn. "
                + "If you did, the magnet is missing, too far from the chip (it wants 1 to 3 mm), or not on the axle; "
                + "the unit's start-up log reports the magnet as detected, too weak or too strong."
        case let .erratic(jumps, steps):
            return "The angle jumps around instead of following the spin (\(jumps) of \(steps) steps were implausibly large). "
                + "The magnet is off-centre, too weak or too strong (keep it 1 to 3 mm from the chip, centred on the axle), or the I2C bus is noisy."
        case let .ok(turns, invalid):
            let turned = String(format: "%.1f", turns)
            return "The AS5600 follows the platter smoothly: \(turned) turns measured"
                + (invalid > 0 ? ", \(invalid) readings failed along the way." : ".")
        }
    }
}

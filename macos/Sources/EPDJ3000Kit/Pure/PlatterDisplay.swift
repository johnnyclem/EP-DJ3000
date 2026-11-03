// Which angle the Decks tab draws for a unit's platter.
//
// Jog messages only describe the motion the unit chose to send. Since contract v1.4 an AS5600 platter whose hold pad has
// registered a touch since power-up sends none while nobody touches it (`jogUntouched` AUTO, or SUPPRESS; PROTOCOL.md
// section 2.2), so a platter that coasts after a flick would sit still on screen if the drawing followed the jog stream. `Telemetry.angle` is the chip's own angle and sees
// every movement, so it is used whenever the telemetry stream is running; the jog stream is the fallback.

import EPDJ3000Protocol

public enum PlatterDisplay {
    public enum Source: Equatable, Sendable {
        /// `Telemetry.angle`: every movement, touched or not.
        case telemetry
        /// The jog messages folded into `ControllerState`: only what the unit sent.
        case jog
    }

    public struct Reading: Equatable, Sendable {
        /// Clockwise as seen from above, in [0, 2 pi), 0 at 12 o'clock: the convention of `PlatterView`.
        public var radians: Double
        public var source: Source
    }

    /// - Parameters:
    ///   - jogRadians: `ControllerState.jogAngleRadians`, the fallback.
    ///   - telemetry: the latest frame, nil while the stream is off.
    ///   - platterSource: telemetry's angle is the chip's only with the AS5600 platter; with the touch ring it is the
    ///     finger's, which exists only under a finger, and jog is what is drawn then.
    ///   - jogInvert: `Telemetry.angle` is raw (before `jogInvert`), jog is not; the setting makes them agree.
    public static func reading(jogRadians: Double, telemetry: Telemetry?, platterSource: PlatterSource, jogInvert: Bool) -> Reading {
        guard platterSource == .as5600, let t = telemetry, StateFlags(rawValue: t.flags).contains(.angleValid) else {
            return Reading(radians: jogRadians, source: .jog)
        }
        let turns = Double(Int(t.angle) & (EncoderDiagnosis.countsPerTurn - 1)) / Double(EncoderDiagnosis.countsPerTurn)
        let signed = (jogInvert ? -turns : turns) * 2 * Double.pi
        let wrapped = signed.truncatingRemainder(dividingBy: 2 * Double.pi)
        return Reading(radians: wrapped < 0 ? wrapped + 2 * Double.pi : wrapped, source: .telemetry)
    }
}

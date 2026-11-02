// Constants tying the jog stream (docs/PROTOCOL.md section 2.2) to playback speed, plus the fader curves.
//
// "Speed" throughout the engine is dimensionless: 1.0 is the track at its own pitch, -1.0 is the same backwards.

import EPDJ3000Protocol

public enum ScratchConstants {
    /// A 33 1/3 rpm record.
    public static let nominalRpm = 100.0 / 3.0
    public static let nominalRevolutionsPerSecond = nominalRpm / 60.0
    /// Jog ticks per second at which the platter reads as speed 1.0: 568.89 (1024 ticks/rev x 0.5556 rev/s).
    public static let nominalTicksPerSecond = nominalRevolutionsPerSecond * Double(Proto.jogTicksPerRev)
    /// Scratch speed limit. The wire format can carry 63 ticks per 2 ms (55x); nobody scratches at that speed.
    public static let maxSpeed = 16.0
    /// Pitch fader span: +-8 %.
    public static let pitchRange = 0.08
    /// Half-width of the pitch fader's centre detent, as a fraction of full travel.
    public static let pitchDetent = 0.004
    /// SHARP_CUT reaches full gain at this fraction of travel. Same knee as firmware/core/src/analog.cpp (6554/65536).
    public static let sharpCutKnee: Float = 6554.0 / 65536.0

    public static func speed(forTicksPerSecond ticksPerSecond: Double) -> Double {
        ticksPerSecond / nominalTicksPerSecond
    }

    public static func ticksPerSecond(forSpeed speed: Double) -> Double {
        speed * nominalTicksPerSecond
    }

    /// Tempo ratio for a pitch fader position in 0...1: centre = 1.0 (with a small detent), ends = 1 -+ `range`.
    /// Higher fader values play faster; the device's `faderInvert` flips the physical direction.
    public static func pitchRatio(faderNormalized x: Double, range: Double = pitchRange) -> Double {
        let d = min(1, max(0, x)) - 0.5
        let m = abs(d)
        guard m > pitchDetent else { return 1 }
        let u = (m - pitchDetent) / (0.5 - pitchDetent)
        return 1 + (d < 0 ? -range : range) * u
    }
}

public extension FaderCurve {
    /// Output gain for a fader position (0 = closed, 1 = open).
    func gain(at position: Float) -> Float {
        guard position.isFinite else { return 0 }
        let x = min(1, max(0, position))
        switch self {
        case .linear: return x
        case .sharpCut: return min(1, x / ScratchConstants.sharpCutKnee)
        case .smooth: return x * x * (3 - 2 * x)
        }
    }
}

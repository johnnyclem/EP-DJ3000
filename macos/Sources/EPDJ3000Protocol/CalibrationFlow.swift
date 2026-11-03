// Which calibration steps a unit needs, and in which order, decided by how its platter is sensed (`platterSource`).
// Pure so the choice is tested on Linux; the app's CalibrationController and CalibrateView only follow it.

public enum CalibrationStep: String, CaseIterable, Identifiable, Sendable {
    /// AS5600 only: does the encoder answer, and does its angle follow a spin (`EncoderDiagnosis`).
    case encoder
    /// Platter direction from `jogTotal` (`PlatterDirectionCalibration`); any source that reports rotation.
    case direction
    /// One touch pad: idle noise, then a hand on the platter (`HoldPadCalibration`).
    case holdPad
    /// The segmented ring, one finger per sector (`TouchRingCalibration`). Experimental.
    case touchRing
    case fader
    case knob

    public var id: String { rawValue }
}

public enum CalibrationFlow {
    /// The order a first calibration runs in.
    public static func steps(for source: PlatterSource) -> [CalibrationStep] {
        switch source {
        case .as5600: return [.encoder, .direction, .holdPad, .fader, .knob]
        case .touchRing: return [.fader, .knob, .touchRing, .direction]
        case .touchHoldOnly: return [.holdPad, .fader, .knob]
        }
    }

    /// The source a unit is using: its `platterSource` setting when the app has read the settings, else what it
    /// reported in `Info`, else the factory default (AS5600).
    public static func source(config: Int32?, info: UInt8?) -> PlatterSource {
        for raw in [config, info.map { Int32($0) }] {
            if let raw, raw >= 0, raw <= Int32(UInt8.max), let source = PlatterSource(rawValue: UInt8(raw)) { return source }
        }
        return .as5600
    }
}

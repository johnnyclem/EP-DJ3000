// How each ConfigKey is edited. `ConfigTable` (generated from the contract) knows names, ranges and defaults but not
// which keys are enums or bit fields, so that knowledge lives here, once. Every key must be listed: a test walks
// `ConfigKey.allCases`, and the `switch` has no `default`, so a key added to the contract fails the build here
// instead of silently getting a bare number field.

import EPDJ3000Protocol

public struct ConfigChoice: Hashable, Sendable {
    public let value: Int32
    public let label: String
}

public enum ConfigKind: Sendable {
    case toggle
    case choice([ConfigChoice])
    /// Each choice's `value` is the bit mask; the setting is their OR.
    case bitfield([ConfigChoice])
    case integer
}

public enum ConfigPresentation {
    public static func kind(for key: ConfigKey) -> ConfigKind {
        switch key {
        case .deckId: return .choice(choices(Deck.self))
        case .midiChannel: return .choice([ConfigChoice(value: 16, label: "Auto (follow deck)")]
                                          + (0..<16).map { ConfigChoice(value: Int32($0), label: "Channel \($0 + 1)") })
        case .mode: return .choice(choices(Mode.self))
        case .platterSource:
            // Hand-written labels: the generated names do not say which one is the factory default or the experiment.
            return .choice([ConfigChoice(value: Int32(PlatterSource.as5600.rawValue), label: "AS5600 encoder (default)"),
                            ConfigChoice(value: Int32(PlatterSource.touchRing.rawValue), label: "Touch ring (experimental)"),
                            ConfigChoice(value: Int32(PlatterSource.touchHoldOnly.rawValue), label: "Hold only")])
        case .jogUntouched:
            // The label that starts with "Auto" is the contract default (ConfigPresentationTests pins that).
            return .choice([ConfigChoice(value: Int32(JogUntouched.auto.rawValue), label: "Auto (suppress coasting once the hold pad has worked)"),
                            ConfigChoice(value: Int32(JogUntouched.report.rawValue), label: "Always send"),
                            ConfigChoice(value: Int32(JogUntouched.suppress.rawValue), label: "Only while touched")])
        case .faderRole: return .choice(choices(FaderRole.self))
        case .faderCurve: return .choice(choices(FaderCurve.self))
        case .knobRole: return .choice(choices(KnobRole.self))
        case .dacMode: return .choice(choices(DacMode.self))
        case .linkBridge: return .choice(choices(LinkBridge.self))
        case .mtcFps: return .choice(MtcFrameRate.allCases.map { ConfigChoice(value: Int32($0.rawValue), label: label(of: $0)) })
        case .clockOutMask:
            return .bitfield([ConfigChoice(value: Int32(ClockPorts.usb.rawValue), label: "USB"),
                              ConfigChoice(value: Int32(ClockPorts.trs.rawValue), label: "TRS jack"),
                              ConfigChoice(value: Int32(ClockPorts.link.rawValue), label: "Radio link")])
        case .jogInvert, .tapPlay, .faderInvert, .knobInvert, .mtcEnable, .linkEnable, .trsMidiEnable:
            return .toggle
        case .touchSectors, .touchOnPermille, .touchOffPermille, .tapMaxMs, .tapMoveTicks,
             .faderRawMin, .faderRawMax, .faderDeadLoPermille, .faderDeadHiPermille,
             .knobRawMin, .knobRawMax, .bpmX10, .ledBrightness, .buttonDebounceMs, .linkGroup:
            return .integer
        }
    }

    public static func section(for key: ConfigKey) -> String {
        switch key {
        case .deckId, .midiChannel, .mode: return "Identity and mode"
        case .platterSource, .jogInvert, .jogUntouched, .touchSectors, .touchOnPermille, .touchOffPermille, .tapPlay, .tapMaxMs, .tapMoveTicks:
            return "Platter"
        case .faderRole, .faderRawMin, .faderRawMax, .faderInvert, .faderDeadLoPermille, .faderDeadHiPermille, .faderCurve:
            return "Slide pot"
        case .knobRole, .knobRawMin, .knobRawMax, .knobInvert: return "Knob"
        case .clockOutMask, .bpmX10, .mtcEnable, .mtcFps: return "Clock, tempo and time code"
        case .dacMode, .linkEnable, .linkBridge, .linkGroup, .trsMidiEnable: return "Audio, link and ports"
        case .ledBrightness, .buttonDebounceMs: return "Misc"
        }
    }

    /// Sections in display order.
    public static let sections = ["Identity and mode", "Platter", "Slide pot", "Knob", "Clock, tempo and time code",
                                  "Audio, link and ports", "Misc"]

    /// PROTOCOL.md section 3.1: these only take effect after a reboot.
    public static func needsReboot(_ key: ConfigKey) -> Bool {
        key == .dacMode || key == .deckId
    }

    /// Keys the unit refuses in SET_CONFIG (NAK NOT_SUPPORTED): the deck id changes only through SET_DECK, which
    /// restarts the unit. An editor shows these but offers a different action to change them.
    public static func isReadOnly(_ key: ConfigKey) -> Bool {
        key == .deckId
    }

    /// Whether the unit looks at this key with the given platter source. The editor greys out what is not used so a
    /// setting that does nothing is not mistaken for one that does.
    public static func isApplicable(_ key: ConfigKey, platterSource: PlatterSource) -> Bool {
        switch key {
        case .touchSectors: return platterSource == .touchRing
        // The ring only moves under a finger and hold-only has no rotation, so only a free-spinning platter has an
        // "untouched" motion to send or not (PROTOCOL.md section 2.2).
        case .jogUntouched: return platterSource == .as5600
        default: return true
        }
    }

    /// Why `isApplicable` said no, for the line under the row.
    public static func inapplicableNote(for key: ConfigKey) -> String? {
        switch key {
        case .touchSectors: return "Only used with the touch ring."
        case .jogUntouched: return "Only used with the AS5600 platter: the touch ring sends jog only under a finger, hold-only sends none."
        default: return nil
        }
    }

    public static func unit(for key: ConfigKey) -> String? {
        switch key {
        case .touchOnPermille, .touchOffPermille, .faderDeadLoPermille, .faderDeadHiPermille: return "per-mille"
        case .tapMaxMs, .buttonDebounceMs: return "ms"
        case .tapMoveTicks: return "ticks"
        case .faderRawMin, .faderRawMax, .knobRawMin, .knobRawMax: return "ADC counts"
        case .bpmX10: return "BPM x 10"
        default: return nil
        }
    }

    /// The label for a choice value, for read-only displays.
    public static func label(for key: ConfigKey, value: Int32) -> String {
        switch kind(for: key) {
        case let .choice(options): return options.first { $0.value == value }?.label ?? "\(value)"
        case .toggle: return value != 0 ? "On" : "Off"
        case let .bitfield(options):
            let names = options.filter { value & $0.value != 0 }.map(\.label)
            return names.isEmpty ? "None" : names.joined(separator: ", ")
        case .integer: return "\(value)"
        }
    }

    // MARK: labels

    private static func choices<E: CaseIterable & RawRepresentable>(_ type: E.Type) -> [ConfigChoice] where E.RawValue == UInt8 {
        E.allCases.map { ConfigChoice(value: Int32($0.rawValue), label: prettify("\($0)")) }
    }

    private static func label(of rate: MtcFrameRate) -> String {
        switch rate {
        case .fps24: return "24 fps"
        case .fps25: return "25 fps"
        case .fps2997Drop: return "29.97 fps drop"
        case .fps30: return "30 fps"
        }
    }

    /// `syncMaster` -> "Sync master", `as5600` -> "AS5600", `dj` -> "DJ".
    static func prettify(_ camel: String) -> String {
        let special: [String: String] = ["dj": "DJ", "as5600": "AS5600", "usb": "USB", "dac": "DAC", "bpm": "BPM", "trs": "TRS"]
        if let s = special[camel] { return s }
        var out = ""
        for ch in camel {
            if ch.isUppercase, !out.isEmpty { out += " " + ch.lowercased() } else { out += out.isEmpty ? ch.uppercased() : String(ch) }
        }
        return out
    }
}

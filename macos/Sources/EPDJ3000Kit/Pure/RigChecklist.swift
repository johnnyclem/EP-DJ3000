// The Rig tab's checklist (docs/ARCHITECTURE.md section 4): what must be true for "two decks and a mixer" to work,
// judged from what the units and macOS report. Pure, so the judgement is testable without hardware.

import EPDJ3000Protocol

public struct RigUnit: Sendable {
    public var name: String
    /// Confirmed by GET_INFO, else nil.
    public var deck: Deck?
    public var caps: Caps?
    /// From the telemetry stream, when it is running.
    public var flags: StateFlags?
    /// From LINK_STATUS, when it has been asked.
    public var peers: [Peer]?

    public init(name: String, deck: Deck?, caps: Caps? = nil, flags: StateFlags? = nil, peers: [Peer]? = nil) {
        self.name = name
        self.deck = deck
        self.caps = caps
        self.flags = flags
        self.peers = peers
    }

    var label: String { deck.map { $0 == .a ? "Deck A" : "Deck B" } ?? name }
}

public struct RigCheck: Identifiable, Equatable, Sendable {
    public enum Status: Sendable { case ok, warning, problem, info }

    public let id: String
    public let title: String
    public let status: Status
    public let detail: String
}

public enum RigChecklist {
    public static func evaluate(units: [RigUnit], audioDeviceNames: [String]) -> [RigCheck] {
        [decks(units), ids(units), link(units), dac(units), usbAudio(units, audioDeviceNames), routing(), mixer()]
    }

    private static func decks(_ units: [RigUnit]) -> RigCheck {
        switch units.count {
        case 0:
            return RigCheck(id: "decks", title: "Decks on USB", status: .problem,
                            detail: "No EP-DJ3000 found. Use a data USB-C cable and check the unit has power.")
        case 1:
            return RigCheck(id: "decks", title: "Decks on USB", status: .warning,
                            detail: "Only \(units[0].label) is on USB. A second deck without a host rides on this one over the radio link (one-cable mode); see the link check.")
        default:
            return RigCheck(id: "decks", title: "Decks on USB", status: .ok, detail: units.map(\.label).joined(separator: " and ") + " are connected.")
        }
    }

    private static func ids(_ units: [RigUnit]) -> RigCheck {
        let decks = units.compactMap(\.deck)
        if Set(decks).count < decks.count {
            return RigCheck(id: "ids", title: "Distinct deck IDs", status: .problem,
                            detail: "Two units both claim the same deck. Hold CUE + REV for 5 s on one of them (or use Settings > Swap deck).")
        }
        for unit in units {
            guard let own = unit.deck, let peers = unit.peers else { continue }
            if peers.contains(where: { $0.deck == own.rawValue || $0.status & PeerStatus.conflict.rawValue != 0 }) {
                return RigCheck(id: "ids", title: "Distinct deck IDs", status: .problem,
                                detail: "\(unit.label) hears another unit claiming the same deck id over the radio link. Change one with Settings > Swap deck.")
            }
        }
        return RigCheck(id: "ids", title: "Distinct deck IDs", status: .ok, detail: "No conflicts seen.")
    }

    private static func link(_ units: [RigUnit]) -> RigCheck {
        let known = units.filter { $0.peers != nil }
        guard !known.isEmpty else {
            return RigCheck(id: "link", title: "Radio link", status: .info, detail: "Waiting for LINK_STATUS from the units.")
        }
        var lines = [String]()
        var anyDown = false
        for unit in known {
            let peers = (unit.peers ?? []).filter { $0.deck != 0xFF }
            if peers.isEmpty {
                anyDown = true
                lines.append("\(unit.label) sees no other deck.")
                continue
            }
            for peer in peers {
                let who = Deck(rawValue: peer.deck).map { $0 == .a ? "Deck A" : "Deck B" } ?? "deck \(peer.deck)"
                var line = "\(unit.label) sees \(who), \(peer.rssi) dBm"
                if peer.rttUs > 0 { line += ", round trip \(peer.rttUs) us" }
                let flags = PeerFlags(rawValue: peer.flags)
                if flags.contains(.forwarding) && !flags.contains(.usbHost) { line += ", riding on this unit's USB (one-cable mode)" }
                lines.append(line + ".")
            }
        }
        // A single unit whose only job is to be the host has nothing to link to; that is a warning, not a failure.
        let alone = units.count == 1
        return RigCheck(id: "link", title: "Radio link", status: anyDown ? (alone ? .info : .warning) : .ok, detail: lines.joined(separator: " "))
    }

    private static func dac(_ units: [RigUnit]) -> RigCheck {
        let known = units.compactMap { u in u.caps.map { (u, $0) } }
        guard !known.isEmpty else {
            return RigCheck(id: "dac", title: "DAC on each deck", status: .info, detail: "Capabilities not read yet.")
        }
        let missing = known.filter { !$0.1.contains(.dac) }.map { $0.0.label }
        if missing.isEmpty {
            return RigCheck(id: "dac", title: "DAC on each deck", status: .ok, detail: "A DAC is fitted on every connected deck.")
        }
        return RigCheck(id: "dac", title: "DAC on each deck", status: .warning,
                        detail: "\(missing.joined(separator: ", ")): no DAC detected, so no USB audio (MIDI still works). Fit the DAC or set dacMode to ON.")
    }

    private static func usbAudio(_ units: [RigUnit], _ audioDeviceNames: [String]) -> RigCheck {
        var lines = [String]()
        var worst = RigCheck.Status.ok
        func raise(_ s: RigCheck.Status) {
            if s == .problem || (s == .warning && worst == .ok) { worst = s }
        }
        for unit in units {
            guard let caps = unit.caps else { continue }
            guard caps.contains(.dac) else { continue }   // reported by the DAC check
            guard caps.contains(.usbAudio) else {
                lines.append("\(unit.label): DAC present but its USB audio function is off (dacMode OFF, or reboot after changing it).")
                raise(.warning)
                continue
            }
            let visible = unit.deck.map { deck in audioDeviceNames.contains { DeckNameParser.audioDevice(named: $0, isDeck: deck) } } ?? false
            if visible {
                lines.append("\(unit.label): macOS lists its audio output.")
            } else {
                lines.append("\(unit.label): enumerates as audio, but macOS shows no matching output device. Replug it and check Audio MIDI Setup.")
                raise(.problem)
            }
        }
        if lines.isEmpty {
            return RigCheck(id: "usbAudio", title: "USB audio visible to macOS", status: .info, detail: "Nothing to check: no deck reports a DAC yet.")
        }
        return RigCheck(id: "usbAudio", title: "USB audio visible to macOS", status: worst, detail: lines.joined(separator: " "))
    }

    private static func routing() -> RigCheck {
        RigCheck(id: "routing", title: "DJ software routing", status: .info,
                 detail: "Send DJ-software deck 1 to the output named \"EP-DJ3000 Deck A\" and deck 2 to \"EP-DJ3000 Deck B\". Each deck's line out goes to one channel of the EP-136; the mixing happens there. Use the tone buttons below to confirm each path.")
    }

    private static func mixer() -> RigCheck {
        RigCheck(id: "mixer", title: "Mixer", status: .info,
                 detail: "The EP-136 K.O.-SIDEKICK is treated as an audio-only mixer: this app cannot see or control it.")
    }
}

// Recognising the unit by name. The USB product string is `EP-DJ3000 Deck A` / `EP-DJ3000 Deck B`
// (docs/PROTOCOL.md section 1); CoreMIDI and CoreAudio both surface it, possibly with a suffix. A name is only a hint:
// GET_INFO decides which deck a unit is.

import EPDJ3000Protocol

public enum DeckNameParser {
    public static let productName = "EP-DJ3000"

    public static func isEPDJ3000(_ name: String) -> Bool {
        name.lowercased().contains(productName.lowercased())
    }

    /// "EP-DJ3000 Deck A" -> .a. Nil when the name does not say.
    public static func deck(fromName name: String) -> Deck? {
        let words = name.lowercased()
            .split(whereSeparator: { !$0.isLetter && !$0.isNumber })
            .map(String.init)
        for (i, word) in words.enumerated() where word == "deck" && i + 1 < words.count {
            switch words[i + 1] {
            case "a": return .a
            case "b": return .b
            default: continue
            }
        }
        return nil
    }

    /// Does a CoreAudio device name belong to this deck's USB audio function?
    /// A name that does not say which deck matches both, which is only right with a single unit.
    public static func audioDevice(named name: String, isDeck deck: Deck) -> Bool {
        guard isEPDJ3000(name) else { return false }
        return Self.deck(fromName: name).map { $0 == deck } ?? true
    }
}

import Foundation
import XCTest
@testable import EPDJ3000KitPure
import EPDJ3000Protocol

final class DeckNameParserTests: XCTestCase {
    func testDeckFromName() {
        XCTAssertEqual(DeckNameParser.deck(fromName: "EP-DJ3000 Deck A"), .a)
        XCTAssertEqual(DeckNameParser.deck(fromName: "EP-DJ3000 Deck B MIDI 1"), .b)
        XCTAssertEqual(DeckNameParser.deck(fromName: "ep-dj3000 deck b"), .b)
        XCTAssertEqual(DeckNameParser.deck(fromName: "EP-DJ3000 Deck-A"), .a)
        XCTAssertNil(DeckNameParser.deck(fromName: "EP-DJ3000"))
        XCTAssertNil(DeckNameParser.deck(fromName: "EP-DJ3000 Deck AB"))
        XCTAssertNil(DeckNameParser.deck(fromName: "EP-DJ3000 Deck"))
        XCTAssertNil(DeckNameParser.deck(fromName: "Deck"))
    }

    func testProductMatch() {
        XCTAssertTrue(DeckNameParser.isEPDJ3000("EP-DJ3000 Deck A"))
        XCTAssertTrue(DeckNameParser.isEPDJ3000("ep-dj3000"))
        XCTAssertFalse(DeckNameParser.isEPDJ3000("EP-133 K.O. II"))
        XCTAssertFalse(DeckNameParser.isEPDJ3000("Deck A"))
    }

    func testAudioDeviceMatch() {
        XCTAssertTrue(DeckNameParser.audioDevice(named: "EP-DJ3000 Deck A", isDeck: .a))
        XCTAssertFalse(DeckNameParser.audioDevice(named: "EP-DJ3000 Deck A", isDeck: .b))
        XCTAssertTrue(DeckNameParser.audioDevice(named: "EP-DJ3000", isDeck: .b), "an unnamed deck matches either")
        XCTAssertFalse(DeckNameParser.audioDevice(named: "MacBook Pro Speakers", isDeck: .a))
    }
}

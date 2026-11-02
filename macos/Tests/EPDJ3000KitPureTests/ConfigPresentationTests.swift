import Foundation
import XCTest
@testable import EPDJ3000KitPure
import EPDJ3000Protocol

final class ConfigPresentationTests: XCTestCase {
    func testEveryKeyHasASectionThatIsListed() {
        for key in ConfigKey.allCases {
            XCTAssertTrue(ConfigPresentation.sections.contains(ConfigPresentation.section(for: key)), "\(key)")
        }
        XCTAssertEqual(Set(ConfigKey.allCases.map(ConfigPresentation.section(for:))), Set(ConfigPresentation.sections))
    }

    func testChoicesStayInsideTheContractRange() {
        for desc in ConfigTable.all {
            switch ConfigPresentation.kind(for: desc.key) {
            case let .choice(options):
                XCTAssertFalse(options.isEmpty)
                for o in options {
                    XCTAssertTrue((desc.minValue...desc.maxValue).contains(o.value), "\(desc.name) offers \(o.value)")
                }
                XCTAssertEqual(Set(options.map(\.value)).count, options.count, "\(desc.name) has duplicate values")
                XCTAssertTrue(options.contains { $0.value == desc.defaultValue }, "\(desc.name) default is selectable")
                XCTAssertEqual(Set(options.map(\.value)), Set(desc.minValue...desc.maxValue), "\(desc.name) offers the whole range")
            case let .bitfield(bits):
                let mask = bits.reduce(Int32(0)) { $0 | $1.value }
                XCTAssertEqual(mask, desc.maxValue, "\(desc.name) mask covers the range")
                for b in bits { XCTAssertEqual(b.value.nonzeroBitCount, 1) }
            case .toggle:
                XCTAssertEqual(desc.minValue, 0, desc.name)
                XCTAssertEqual(desc.maxValue, 1, desc.name)
            case .integer:
                XCTAssertLessThan(desc.minValue, desc.maxValue)
            }
        }
    }

    func testLabels() {
        XCTAssertEqual(ConfigPresentation.label(for: .mode, value: 1), "Sync master")
        XCTAssertEqual(ConfigPresentation.label(for: .platterSource, value: 0), "AS5600")
        XCTAssertEqual(ConfigPresentation.label(for: .mode, value: 0), "DJ")
        XCTAssertEqual(ConfigPresentation.label(for: .midiChannel, value: 16), "Auto (follow deck)")
        XCTAssertEqual(ConfigPresentation.label(for: .midiChannel, value: 1), "Channel 2")
        XCTAssertEqual(ConfigPresentation.label(for: .clockOutMask, value: 3), "USB, TRS jack")
        XCTAssertEqual(ConfigPresentation.label(for: .clockOutMask, value: 0), "None")
        XCTAssertEqual(ConfigPresentation.label(for: .jogInvert, value: 1), "On")
        XCTAssertEqual(ConfigPresentation.label(for: .mtcFps, value: 2), "29.97 fps drop")
        XCTAssertEqual(ConfigPresentation.label(for: .bpmX10, value: 1285), "1285")
        XCTAssertEqual(ConfigPresentation.label(for: .deckId, value: 9), "9", "an unknown value falls back to the number")
    }

    func testRebootKeys() {
        XCTAssertEqual(ConfigKey.allCases.filter(ConfigPresentation.needsReboot), [.deckId, .dacMode])
    }

    func testOnlyTheDeckIdIsReadOnlyBecauseTheUnitRefusesItInSetConfig() {
        XCTAssertEqual(ConfigKey.allCases.filter(ConfigPresentation.isReadOnly), [.deckId])
        XCTAssertEqual(ConfigPresentation.label(for: .deckId, value: 1), "B", "still shown, by name")
    }

    func testLinkGroupIsAnOrdinaryNumberFieldInTheLinkSection() {
        guard case .integer = ConfigPresentation.kind(for: .linkGroup) else { return XCTFail("linkGroup should use the generic number editor") }
        XCTAssertEqual(ConfigPresentation.section(for: .linkGroup), "Audio, link and ports")
        XCTAssertFalse(ConfigPresentation.isReadOnly(.linkGroup))
        XCTAssertFalse(ConfigPresentation.needsReboot(.linkGroup), "the group applies live")
        let desc = ConfigTable.desc(.linkGroup)
        XCTAssertEqual(desc.minValue, 0)
        XCTAssertEqual(desc.maxValue, 255)
        XCTAssertEqual(ConfigPresentation.label(for: .linkGroup, value: 7), "7")
    }

    func testPrettify() {
        XCTAssertEqual(ConfigPresentation.prettify("syncFollow"), "Sync follow")
        XCTAssertEqual(ConfigPresentation.prettify("touchHoldOnly"), "Touch hold only")
        XCTAssertEqual(ConfigPresentation.prettify("a"), "A")
    }
}

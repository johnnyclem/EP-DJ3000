import Foundation
import XCTest
@testable import EPDJ3000KitPure
import EPDJ3000Protocol

final class MidiFormatTests: XCTestCase {
    func testDescribeNotesAndControllers() {
        XCTAssertEqual(MidiFormat.describe(.noteOn(channel: 0, note: 36, velocity: 127)), "Note On  ch1  36 CUE  vel 127")
        XCTAssertEqual(MidiFormat.describe(.noteOff(channel: 1, note: 38, velocity: 0)), "Note Off ch2  38 TOUCH  vel 0")
        XCTAssertEqual(MidiFormat.describe(.noteOn(channel: 0, note: 60, velocity: 1)), "Note On  ch1  60  vel 1")
        XCTAssertEqual(MidiFormat.describe(.controlChange(channel: 0, controller: 22, value: 67)), "CC       ch1  22 jog  +3")
        XCTAssertEqual(MidiFormat.describe(.controlChange(channel: 0, controller: 22, value: 60)), "CC       ch1  22 jog  -4")
        XCTAssertEqual(MidiFormat.describe(.controlChange(channel: 0, controller: 23, value: 100)), "CC       ch1  23 crossfader MSB  100")
        XCTAssertEqual(MidiFormat.describe(.controlChange(channel: 0, controller: 29, value: 1)), "CC       ch1  29 transport  playing")
        XCTAssertEqual(MidiFormat.describe(.controlChange(channel: 0, controller: 21, value: 65)), "CC       ch1  21 BPM nudge  +1")
        XCTAssertEqual(MidiFormat.describe(.songPosition(beats: 16)), "Song Position  16 beats")
        XCTAssertEqual(MidiFormat.describe(.clock), "Clock")
    }

    func testEveryDocumentedControllerHasAName() {
        for cc: UInt8 in [7, 39, 20, 52, 21, 22, 23, 55, 24, 56, 25, 57, 29] {
            XCTAssertNotNil(MidiFormat.controllerName(cc), "CC \(cc)")
        }
        for note: UInt8 in [36, 37, 38, 39, 41] {
            XCTAssertNotNil(MidiFormat.noteName(note), "note \(note)")
        }
        XCTAssertNil(MidiFormat.controllerName(1))
    }

    func testCategories() {
        XCTAssertEqual(MidiFormat.category(of: .noteOn(channel: 0, note: 1, velocity: 1)), .notes)
        XCTAssertEqual(MidiFormat.category(of: .controlChange(channel: 0, controller: 22, value: 64)), .jog)
        XCTAssertEqual(MidiFormat.category(of: .controlChange(channel: 0, controller: 23, value: 64)), .controlChange)
        XCTAssertEqual(MidiFormat.category(of: .clock), .clock)
        XCTAssertEqual(MidiFormat.category(of: .start), .transport)
        XCTAssertEqual(MidiFormat.category(of: .songPosition(beats: 0)), .transport)
        XCTAssertEqual(MidiFormat.category(of: .mtcQuarterFrame(piece: 0, nibble: 0)), .timecode)
        XCTAssertEqual(MidiFormat.category(of: .sysex([0xF0, 0xF7])), .sysex)
        XCTAssertEqual(MidiFormat.category(of: .other(status: 0xC0, data: [1])), .other)
    }

    func testHex() {
        XCTAssertEqual(MidiFormat.hex([0xF0, 0x0A, 0x7D]), "F0 0A 7D")
        XCTAssertEqual(MidiFormat.hex([1, 2, 3, 4], limit: 2), "01 02 ... (4 B)")
        XCTAssertEqual(MidiFormat.hex([]), "")
    }

    func testTelemetryFrameDetection() {
        let telemetry = SysExFrame(deck: 0, reply: .telemetry, payload: Telemetry().serialized()).encode()
        XCTAssertTrue(MidiFormat.isTelemetryFrame(telemetry))
        XCTAssertFalse(MidiFormat.isTelemetryFrame(SysExFrame(deck: 127, command: .getInfo).encode()))
        XCTAssertFalse(MidiFormat.isTelemetryFrame([0xF0, 0x7D, 0xF7]))
        XCTAssertFalse(MidiFormat.isTelemetryFrame([0xF0, 0x7E, 0x45, 0x44, 1, 0, 0x47, 0, 0xF7]))
    }

    func testSysExDescriptions() {
        XCTAssertTrue(MidiFormat.describeSysEx(SysExFrame(deck: 127, command: .getInfo).encode()).contains("GET_INFO"))
        XCTAssertTrue(MidiFormat.describeSysEx(SysExFrame(deck: 127, command: .getInfo).encode()).contains("deck any"))
        let set = SysExFrame(deck: 0, command: .setConfig, payload: [ConfigKey.faderRawMin.rawValue, 40, 0, 0, 0]).encode()
        XCTAssertTrue(MidiFormat.describeSysEx(set).contains("SET_CONFIG  faderRawMin = 40"), MidiFormat.describeSysEx(set))
        let reply = SysExFrame(deck: 1, reply: .config, payload: [ConfigKey.bpmX10.rawValue, 0xB0, 0x04, 0, 0]).encode()
        XCTAssertTrue(MidiFormat.describeSysEx(reply).contains("CONFIG  bpmX10 = 1200"), MidiFormat.describeSysEx(reply))
        let nak = SysExFrame.nak(deck: 0, command: SysExCmd.testTone.rawValue, error: .notSupported).encode()
        XCTAssertTrue(MidiFormat.describeSysEx(nak).contains("NAK  TEST_TONE  notSupported"), MidiFormat.describeSysEx(nak))
        let ack = SysExFrame.ack(deck: 0, command: SysExCmd.saveConfig.rawValue).encode()
        XCTAssertTrue(MidiFormat.describeSysEx(ack).contains("ACK  SAVE_CONFIG"))
        XCTAssertTrue(MidiFormat.describeSysEx([0xF0, 0x41, 0x10, 0xF7]).contains("other device"))
        var corrupt = SysExFrame(deck: 127, command: .getInfo).encode()
        corrupt[corrupt.count - 2] ^= 0x01
        XCTAssertTrue(MidiFormat.describeSysEx(corrupt).contains("bad EP-DJ3000 frame"))
        let mtc: [UInt8] = [0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x61, 0x02, 0x03, 0x04, 0xF7]
        XCTAssertTrue(MidiFormat.describeSysEx(mtc).contains("MTC full frame"), MidiFormat.describeSysEx(mtc))
    }

    func testEveryCommandAndReplyHasAName() {
        for c in SysExCmd.allCases { XCTAssertFalse(MidiFormat.commandName(c).isEmpty) }
        for r in SysExReply.allCases { XCTAssertFalse(MidiFormat.replyName(r).isEmpty) }
        XCTAssertEqual(Set(SysExCmd.allCases.map(MidiFormat.commandName)).count, SysExCmd.allCases.count, "names are unique")
    }
}

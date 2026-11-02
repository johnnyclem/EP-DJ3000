import XCTest
@testable import EPDJ3000Protocol

final class UsbMidiTests: XCTestCase {
    func testCinByteCounts() {
        let expected: [UInt8: Int] = [0x0: 0, 0x1: 0, 0x2: 2, 0x3: 3, 0x4: 3, 0x5: 1, 0x6: 2, 0x7: 3,
                                      0x8: 3, 0x9: 3, 0xA: 3, 0xB: 3, 0xC: 2, 0xD: 2, 0xE: 3, 0xF: 1]
        for (cin, n) in expected { XCTAssertEqual(UsbMidi.byteCount(cin: cin), n, "CIN \(cin)") }
    }

    func testSysExPacketisationForEveryLength() throws {
        for length in 2...14 {
            let message = [0xF0] + (1..<(length - 1)).map { UInt8($0) } + [0xF7]
            let wire = try XCTUnwrap(UsbMidi.encode(cable: 2, message: message), "length \(length)")
            XCTAssertEqual(wire.count % 4, 0)
            let packets = UsbMidi.decode(wire)
            XCTAssertEqual(packets.flatMap(\.bytes), message, "length \(length)")
            XCTAssertTrue(packets.dropLast().allSatisfy { $0.cin == 0x4 }, "length \(length): only the last packet ends the SysEx")
            XCTAssertEqual(packets.last?.cin, [0x7, 0x5, 0x6][length % 3], "length \(length)")  // 3->7, 1->5, 2->6
            XCTAssertTrue(packets.allSatisfy { $0.cable == 2 })
        }
    }

    func testEncodeRejectsWhatIsNotExactlyOneMessage() {
        XCTAssertNil(UsbMidi.encode(cable: 0, message: []))
        XCTAssertNil(UsbMidi.encode(cable: 0, message: [0x40, 0x40]), "data byte first")
        XCTAssertNil(UsbMidi.encode(cable: 16, message: [0xF8]), "cable is a nibble")
        XCTAssertNil(UsbMidi.encode(cable: 0, message: [0x90, 36]), "truncated note on")
        XCTAssertNil(UsbMidi.encode(cable: 0, message: [0x90, 36, 127, 0]), "two messages")
        XCTAssertNil(UsbMidi.encode(cable: 0, message: [0x90, 36, 0x90]), "status byte as data")
        XCTAssertNil(UsbMidi.encode(cable: 0, message: [0xF0, 1, 2]), "SysEx without F7")
        XCTAssertNil(UsbMidi.encode(cable: 0, message: [0xF0, 0x80, 0xF7]), "high bit inside SysEx")
        XCTAssertNil(UsbMidi.encode(cable: 0, message: [0xC0]), "program change without data")
    }

    func testEncodeSystemAndChannelMessages() {
        XCTAssertEqual(UsbMidi.encode(cable: 3, message: [0xFC]), [0x3F, 0xFC, 0, 0])
        XCTAssertEqual(UsbMidi.encode(cable: 0, message: [0xF3, 5]), [0x02, 0xF3, 5, 0])
        XCTAssertEqual(UsbMidi.encode(cable: 0, message: [0xF6]), [0x05, 0xF6, 0, 0])
        XCTAssertEqual(UsbMidi.encode(cable: 1, message: [0xD0, 9]), [0x1D, 0xD0, 9, 0])
        XCTAssertEqual(UsbMidi.encode(cable: 1, message: [0xE5, 0, 64]), [0x1E, 0xE5, 0, 64])
        XCTAssertEqual(UsbMidi.encode(cable: 0, message: [0xF0, 0xF7]), [0x06, 0xF0, 0xF7, 0])
    }

    func testDecodeSkipsPaddingReservedCinsAndPartialTrailingPacket() {
        let wire: [UInt8] = [0x00, 0, 0, 0,          // padding
                             0x01, 1, 2, 3,          // reserved cable event
                             0x09, 0x90, 36, 127,    // note on
                             0x29, 0x91]             // truncated by the end of the transfer
        let packets = UsbMidi.decode(wire)
        XCTAssertEqual(packets, [UsbMidiPacket(cable: 0, cin: 0x9, bytes: [0x90, 36, 127])])
        XCTAssertEqual(UsbMidi.decode([]), [])
    }

    func testPacketsOnDifferentCablesKeepTheirCable() {
        let wire = UsbMidi.encode(cable: 0, message: [0xF8])! + UsbMidi.encode(cable: 2, message: [0x91, 39, 127])!
        XCTAssertEqual(UsbMidi.decode(wire).map(\.cable), [0, 2])
    }
}

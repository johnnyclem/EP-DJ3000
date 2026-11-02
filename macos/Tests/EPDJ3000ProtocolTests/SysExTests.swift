import XCTest
@testable import EPDJ3000Protocol

final class SysExTests: XCTestCase {
    private let good = VectorFile.shared.frame("setConfig platterSource=1 deck B").frame

    /// Re-seals a frame body (ver ... last septet) with a correct checksum.
    private func seal(_ body: [UInt8]) -> [UInt8] {
        [0xF0] + SysExCodec.header + body + [SysExCodec.checksum(body), 0xF7]
    }

    func testRoundTripsEveryPayloadLengthWithAllByteValues() throws {
        var seed: UInt32 = 12345
        func next() -> UInt8 {
            seed = seed &* 1664525 &+ 1013904223
            return UInt8(truncatingIfNeeded: seed >> 24)
        }
        for length in 0...64 {
            let payload = (0..<length).map { _ in next() }
            let frame = SysExFrame(deck: UInt8(length % 16), code: SysExCmd.setConfig.rawValue, payload: payload)
            let wire = frame.encode()
            XCTAssertTrue(wire[1..<(wire.count - 1)].allSatisfy { $0 < 0x80 }, "length \(length): body must be 7-bit clean")
            XCTAssertEqual(try SysExFrame.decode(wire), frame, "length \(length)")
        }
    }

    func testUnpackRejectsWhatPackCannotProduce() {
        XCTAssertNil(SysExCodec.unpack7to8([0x00]), "header septet with no data")
        XCTAssertNil(SysExCodec.unpack7to8([0, 1, 2, 3, 4, 5, 6, 7, 0x01]), "trailing header septet")
        XCTAssertNil(SysExCodec.unpack7to8([0x00, 0x80]), "data septet with bit 7 set")
        XCTAssertNil(SysExCodec.unpack7to8([0x80, 0x01]), "header septet with bit 7 set")
        XCTAssertEqual(SysExCodec.unpack7to8([]), [])
    }

    func testChecksumMatchesDefinition() {
        XCTAssertEqual(SysExCodec.checksum([UInt8]()), 0)
        XCTAssertEqual(SysExCodec.checksum([1, 0, 1]), 126)
        XCTAssertEqual(SysExCodec.checksum([0x7F, 0x7F]), 2)
        for body in [[1, 2, 3], [127, 127, 127, 5], [0, 0, 0]] as [[UInt8]] {
            let total = body.reduce(0) { $0 + Int($1) } + Int(SysExCodec.checksum(body))
            XCTAssertEqual(total & 0x7F, 0)
        }
    }

    func testDecodeRejectsMalformedFrames() {
        func assertThrows(_ bytes: [UInt8], _ expected: SysExFrameError, _ what: String, line: UInt = #line) {
            XCTAssertThrowsError(try SysExFrame.decode(bytes), what, line: line) {
                XCTAssertEqual($0 as? SysExFrameError, expected, what, line: line)
            }
        }
        assertThrows([], .missingFraming, "empty")
        assertThrows([0xF0], .missingFraming, "one byte")
        assertThrows(Array(good.dropLast()), .missingFraming, "no F7")
        assertThrows(Array(good.dropFirst()), .missingFraming, "no F0")
        assertThrows([0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7], .notEPDJ3000, "device inquiry")
        assertThrows([0xF0, 0xF7], .notEPDJ3000, "empty sysex")
        var otherModel = good
        otherModel[3] = 0x43
        assertThrows(otherModel, .notEPDJ3000, "wrong model id")
        assertThrows([0xF0, 0x7D, 0x45, 0x44, 0x01, 0x00, 0x01, 0xF7], .tooShort, "no checksum")

        var v2 = good
        v2[4] = 2
        assertThrows(v2, .unsupportedVersion(2), "version 2")

        var corrupt = good
        corrupt[corrupt.count - 3] ^= 0x01
        assertThrows(corrupt, .badChecksum, "flipped payload bit")

        var high = good
        high[8] = 0x80
        assertThrows(high, .dataByteOutOfRange, "bit 7 inside the body")

        assertThrows(seal([Proto.SysEx.version, 0, 0x03, 0x00]), .badPacking, "lone header septet")
    }

    func testFrameKinds() {
        XCTAssertEqual(SysExFrame(deck: 0, command: .getInfo).kind, .command(.getInfo))
        XCTAssertEqual(SysExFrame(deck: 0, reply: .link, payload: [0]).kind, .reply(.link))
        XCTAssertEqual(SysExFrame(deck: 0, code: 0x7E, payload: [4]).kind, .ack(command: 4))
        XCTAssertEqual(SysExFrame(deck: 0, code: 0x7F, payload: [3, 99]).kind, .nak(command: 3, errorCode: 99))
        XCTAssertEqual(SysExFrame(deck: 0, code: 0x7E).kind, .unknown(code: 0x7E), "ACK without a command byte")
        XCTAssertEqual(SysExFrame(deck: 0, code: 0x7F, payload: [3]).kind, .unknown(code: 0x7F), "NAK without an error byte")
        XCTAssertEqual(SysExFrame(deck: 0, code: 0x30).kind, .unknown(code: 0x30))
        XCTAssertNil(SysExError(rawValue: 99))
    }

    func testReplyCodesAreCommandCodesWithTheReplyFlag() {
        for r in SysExReply.allCases {
            XCTAssertNotNil(SysExCmd(rawValue: r.rawValue & ~Proto.SysEx.replyFlag), "\(r)")
            XCTAssertNotEqual(r.rawValue & Proto.SysEx.replyFlag, 0, "\(r)")
        }
        let replyCodes = Set(SysExReply.allCases.map(\.rawValue))
        for c in SysExCmd.allCases {
            XCTAssertFalse(replyCodes.contains(c.rawValue))
            XCTAssertNotEqual(c.rawValue, Proto.SysEx.ack)
            XCTAssertNotEqual(c.rawValue, Proto.SysEx.nak)
        }
    }
}

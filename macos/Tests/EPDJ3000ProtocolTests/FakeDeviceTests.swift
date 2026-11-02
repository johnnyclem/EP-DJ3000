import XCTest
@testable import EPDJ3000Protocol

/// Pins FakeDevice to docs/PROTOCOL.md section 3, request by request, with raw frames and no DeviceClient in between.
/// DeviceClientTests are only as good as the unit they talk to; this is what keeps that unit honest.
final class FakeDeviceTests: XCTestCase {
    /// Sends one request straight to the fake and returns what it answered.
    private func ask(_ dev: FakeDevice, _ command: SysExCmd, _ payload: [UInt8] = [], deck: UInt8 = Proto.SysEx.deckAny) async throws -> [SysExFrame] {
        let before = dev.read { $0.replies.count }
        try await dev.send(SysExFrame(deck: deck, command: command, payload: payload).encode())
        return Array(dev.read { $0.replies }.dropFirst(before))
    }

    private func nak(_ dev: FakeDevice, _ command: SysExCmd, _ error: SysExError, deck: UInt8 = 0) -> SysExFrame {
        .nak(deck: deck, command: command.rawValue, error: error)
    }

    /// Everything a command could change.
    private func fingerprint(_ dev: FakeDevice) -> String {
        dev.read { s in
            "\(s.config) deck \(s.deck) saves \(s.saveCount) stream \(s.streamRate) reboot \(String(describing: s.lastReboot)) "
                + "tone \(String(describing: s.lastTone)) identify \(String(describing: s.lastIdentify)) cal \(String(describing: s.calKind))"
        }
    }

    func testEveryCommandRefusesAnyOtherPayloadLengthAndChangesNothing() async throws {
        for command in SysExCmd.allCases {
            let dev = FakeDevice()
            dev.mutate { $0.config[Int(ConfigKey.tapMaxMs.rawValue)] = 300 }  // so LOAD_DEFAULTS would show
            let before = fingerprint(dev)
            let valid = FakeDevice.payloadLength(command)
            for length in 0...9 where length != valid {
                let reply = try await ask(dev, command, [UInt8](repeating: 1, count: length))
                XCTAssertEqual(reply, [nak(dev, command, .badLength)], "\(command) with \(length) payload bytes")
            }
            XCTAssertEqual(fingerprint(dev), before, "\(command): a malformed request must have no side effects")
        }
    }

    func testAPayloadThatCannotBeUnpackedIsBadLength() async throws {
        let dev = FakeDevice()
        let body: [UInt8] = [Proto.SysEx.version, Proto.SysEx.deckAny, SysExCmd.identify.rawValue, 0x00]  // a header septet, no data
        let raw = [0xF0] + SysExCodec.header + body + [SysExCodec.checksum(body), 0xF7]
        XCTAssertThrowsError(try SysExFrame.decode(raw)) { XCTAssertEqual($0 as? SysExFrameError, .badPacking) }
        try await dev.send(raw)
        XCTAssertEqual(dev.read { $0.replies }, [nak(dev, .identify, .badLength)])
        XCTAssertNil(dev.read { $0.lastIdentify })

        let tooLong = try await ask(dev, .identify, [UInt8](repeating: 0xAA, count: 97))
        XCTAssertEqual(tooLong, [nak(dev, .identify, .badLength)])
    }

    func testAnUnknownCommandIsNaked() async throws {
        let dev = FakeDevice()
        try await dev.send(SysExFrame(deck: 127, code: 0x3F).encode())
        XCTAssertEqual(dev.read { $0.replies }, [.nak(deck: 0, command: 0x3F, error: .unknownCmd)])
    }

    func testADumpIsOneConfigPerKeyThenTheGetConfigAck() async throws {
        let dev = FakeDevice()
        let reply = try await ask(dev, .getConfig, [0xFF])
        XCTAssertEqual(reply.count, ConfigTable.all.count + 1)
        XCTAssertEqual(reply.last, .ack(deck: 0, command: SysExCmd.getConfig.rawValue))
        for (index, frame) in reply.dropLast().enumerated() {
            XCTAssertEqual(frame.kind, .reply(.config))
            XCTAssertEqual(frame.payload.first, UInt8(index), "keys come in id order")
        }
        // A single key is one CONFIG and no ACK.
        let single = try await ask(dev, .getConfig, [ConfigKey.mode.rawValue])
        XCTAssertEqual(single, [SysExFrame(deck: 0, reply: .config, payload: [ConfigKey.mode.rawValue, 0, 0, 0, 0])])
        let missing = try await ask(dev, .getConfig, [UInt8(ConfigTable.all.count)])
        XCTAssertEqual(missing, [nak(dev, .getConfig, .badKey)])
    }

    func testAUnitWithMoreKeysDumpsThemToo() async throws {
        let dev = FakeDevice()
        dev.mutate { $0.keyCount = ConfigTable.all.count + 2 }
        let reply = try await ask(dev, .getConfig, [0xFF])
        XCTAssertEqual(reply.count, ConfigTable.all.count + 3)
        XCTAssertEqual(reply.last?.kind, .ack(command: SysExCmd.getConfig.rawValue))
        let extra = try await ask(dev, .getConfig, [UInt8(ConfigTable.all.count)])
        XCTAssertEqual(extra.first?.kind, .reply(.config))
    }

    func testSetConfigOfTheDeckIdIsNotSupportedWhateverTheValue() async throws {
        let dev = FakeDevice(deck: .a)
        for value: UInt8 in [0, 1, 7] {
            let reply = try await ask(dev, .setConfig, [ConfigKey.deckId.rawValue, value, 0, 0, 0])
            XCTAssertEqual(reply, [nak(dev, .setConfig, .notSupported)], "value \(value)")
        }
        XCTAssertEqual(dev.read { $0.deck }, 0)
        XCTAssertEqual(dev.read { $0.config[0] }, 0)
    }

    func testSetDeckAcksWithTheOldDeckThenTheUnitIsTheNewOne() async throws {
        let dev = FakeDevice(deck: .a)
        let reply = try await ask(dev, .setDeck, [1], deck: 0)
        XCTAssertEqual(reply, [.ack(deck: 0, command: SysExCmd.setDeck.rawValue)])
        XCTAssertEqual(dev.read { $0.deck }, 1)
        XCTAssertEqual(dev.read { $0.config[0] }, 1, "deckId follows")
        let oldId = try await ask(dev, .getInfo, [], deck: 0)
        XCTAssertTrue(oldId.isEmpty, "it no longer answers to the old id")
        let info = try await ask(dev, .getInfo, [], deck: 1)
        XCTAssertEqual(Info(bytes: info[0].payload)?.deck, 1)
        XCTAssertEqual(info[0].deck, 1)

        let refused = try await ask(dev, .setDeck, [2], deck: 1)
        XCTAssertEqual(refused, [nak(dev, .setDeck, .outOfRange, deck: 1)])
        XCTAssertEqual(dev.read { $0.deck }, 1)
    }

    func testLoadDefaultsKeepsTheDeckIdAndResetsTheRest() async throws {
        let dev = FakeDevice(deck: .b)
        dev.mutate { $0.config[Int(ConfigKey.tapMaxMs.rawValue)] = 300 }
        let reply = try await ask(dev, .loadDefaults, [], deck: 1)
        XCTAssertEqual(reply, [.ack(deck: 1, command: SysExCmd.loadDefaults.rawValue)])
        XCTAssertEqual(dev.read { $0.config[0] }, 1)
        XCTAssertEqual(dev.read { $0.config[Int(ConfigKey.tapMaxMs.rawValue)] }, 180)
    }

    func testTestToneChecksMatchTheContract() async throws {
        let dev = FakeDevice()
        func tone(_ on: UInt8, _ freq: UInt16, _ atten: UInt8) async throws -> SysExFrame? {
            try await ask(dev, .testTone, [on, UInt8(freq & 0xFF), UInt8(freq >> 8), atten]).first
        }
        let ok = SysExFrame.ack(deck: 0, command: SysExCmd.testTone.rawValue)
        let range = nak(dev, .testTone, .outOfRange)
        let accepted = try await [tone(1, 20, 0), tone(1, 20000, 96), tone(0, 0, 255), tone(0, 65535, 0)]
        XCTAssertEqual(accepted, [ok, ok, ok, ok], "edges are in; an off request ignores frequency and attenuation")
        let refused = try await [tone(2, 440, 12), tone(255, 440, 12), tone(1, 19, 12), tone(1, 20001, 12), tone(1, 440, 97)]
        XCTAssertEqual(refused, [range, range, range, range, range])

        dev.mutate { $0.hasDac = false }
        let noDac = try await tone(1, 440, 12)
        XCTAssertEqual(noDac, nak(dev, .testTone, .notSupported))
    }

    func testBooleansAndEnumsAboveTheirRangeAreOutOfRange() async throws {
        let dev = FakeDevice()
        let range = [nak(dev, .calEnd, .outOfRange)]
        _ = try await ask(dev, .calBegin, [0])
        let badApply = try await ask(dev, .calEnd, [0, 2])
        XCTAssertEqual(badApply, range)
        XCTAssertEqual(dev.read { $0.calKind }, .fader, "the refusal did not end the calibration")

        let badReboot = try await ask(dev, .reboot, [2])
        XCTAssertEqual(badReboot, [nak(dev, .reboot, .outOfRange)])
        XCTAssertNil(dev.read { $0.lastReboot })
        let badDeck = try await ask(dev, .setDeck, [2])
        XCTAssertEqual(badDeck, [nak(dev, .setDeck, .outOfRange)])
        let badValue = try await ask(dev, .setConfig, [ConfigKey.jogInvert.rawValue, 2, 0, 0, 0])
        XCTAssertEqual(badValue, [nak(dev, .setConfig, .outOfRange)])
    }

    func testCalibrationKindRules() async throws {
        let dev = FakeDevice()
        let unknownKind = try await ask(dev, .calBegin, [2])
        XCTAssertEqual(unknownKind, [nak(dev, .calBegin, .notSupported)])
        let notActive = try await ask(dev, .calEnd, [0, 1])
        XCTAssertEqual(notActive, [nak(dev, .calEnd, .calNotActive)])
        _ = try await ask(dev, .calBegin, [0])
        let otherKind = try await ask(dev, .calEnd, [1, 1])
        XCTAssertEqual(otherKind, [nak(dev, .calEnd, .calNotActive)])
        let unknownEnd = try await ask(dev, .calEnd, [9, 1])
        XCTAssertEqual(unknownEnd, [nak(dev, .calEnd, .calNotActive)])
    }

    func testStreamRatesAboveTheLimitAreClampedNotRefused() async throws {
        let dev = FakeDevice()
        let reply = try await ask(dev, .stream, [200])
        XCTAssertEqual(reply, [.ack(deck: 0, command: SysExCmd.stream.rawValue)])
        XCTAssertEqual(dev.read { $0.streamRate }, 100)
    }

    func testADeckOtherThanTheUnitsIsNotAnswered() async throws {
        let dev = FakeDevice(deck: .a)
        let other = try await ask(dev, .getInfo, [], deck: 1)
        XCTAssertTrue(other.isEmpty)
        let any = try await ask(dev, .getInfo, [], deck: Proto.SysEx.deckAny)
        XCTAssertEqual(any.count, 1)
    }
}

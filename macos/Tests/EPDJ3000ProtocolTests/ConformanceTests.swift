import XCTest
@testable import EPDJ3000Protocol

/// Every entry of protocol/test-vectors.json, read from the file, plus drift checks against protocol/epdj3000.json.
final class ConformanceTests: XCTestCase {
    private let v = VectorFile.shared

    func testPack8to7Vectors() {
        XCTAssertFalse(v.pack8to7.isEmpty)
        for p in v.pack8to7 {
            XCTAssertEqual(SysExCodec.pack8to7(p.raw), p.packed, "pack \(p.raw)")
            XCTAssertEqual(SysExCodec.unpack7to8(p.packed), p.raw, "unpack \(p.packed)")
        }
    }

    func testSysExFrameVectors() throws {
        XCTAssertFalse(v.sysexFrames.isEmpty)
        for f in v.sysexFrames {
            let frame = SysExFrame(deck: f.deck, code: f.cmd, payload: f.payload)
            XCTAssertEqual(frame.encode(), f.frame, f.name)
            XCTAssertEqual(try SysExFrame.decode(f.frame), frame, f.name)
        }
    }

    func testAckAndNakVectorsMapToTypedKinds() throws {
        let ack = try SysExFrame.decode(v.frame("ack for setConfig").frame)
        XCTAssertEqual(ack.kind, .ack(command: SysExCmd.setConfig.rawValue))

        let nak = try SysExFrame.decode(v.frame("nak(setConfig, OUT_OF_RANGE)").frame)
        XCTAssertEqual(nak.kind, .nak(command: SysExCmd.setConfig.rawValue, errorCode: 5))
        XCTAssertEqual(SysExError(rawValue: 5), .outOfRange)

        XCTAssertEqual(SysExFrame.ack(deck: 0, command: 3).encode(), v.frame("ack for setConfig").frame)
        XCTAssertEqual(SysExFrame.nak(deck: 0, command: 3, error: .outOfRange).encode(), v.frame("nak(setConfig, OUT_OF_RANGE)").frame)
    }

    func testReplyFramesCarryTheStructVectors() throws {
        let info = try SysExFrame.decode(v.frame("info reply").frame)
        XCTAssertEqual(info.kind, .reply(.info))
        XCTAssertEqual(Info(bytes: info.payload)?.serial, 0xDEADBEEF)

        let tel = try SysExFrame.decode(v.frame("telemetry").frame)
        XCTAssertEqual(tel.kind, .reply(.telemetry))
        XCTAssertEqual(Telemetry(bytes: tel.payload)?.jogTotal, -70000)
    }

    func testStructVectors() {
        XCTAssertEqual(v.structs.map(\.struct), ["Info", "Telemetry", "CalResult", "Peer"])
        for s in v.structs {
            switch s.struct {
            case "Info":
                let x = Info(protocolVersion: UInt8(s.int("protocolVersion")), fwMajor: UInt8(s.int("fwMajor")),
                             fwMinor: UInt8(s.int("fwMinor")), fwPatch: UInt8(s.int("fwPatch")), hwRev: UInt8(s.int("hwRev")),
                             deck: UInt8(s.int("deck")), mode: UInt8(s.int("mode")), platterSource: UInt8(s.int("platterSource")),
                             caps: UInt16(s.int("caps")), serial: UInt32(s.int("serial")), uptimeS: UInt32(s.int("uptimeS")))
                XCTAssertEqual(x.serialized(), s.bytes)
                XCTAssertEqual(Info(bytes: s.bytes), x)
            case "Telemetry":
                let x = Telemetry(seq: UInt8(s.int("seq")), buttons: UInt8(s.int("buttons")), flags: UInt8(s.int("flags")),
                                  transport: UInt8(s.int("transport")), faderRaw: UInt16(s.int("faderRaw")),
                                  knobRaw: UInt16(s.int("knobRaw")), faderOut: UInt16(s.int("faderOut")),
                                  knobOut: UInt16(s.int("knobOut")), angle: UInt16(s.int("angle")), bpmX10: UInt16(s.int("bpmX10")),
                                  jogTotal: Int32(s.int("jogTotal")), touchRaw: s.list("touchRaw").map { UInt16($0) })
                XCTAssertEqual(x.serialized(), s.bytes)
                XCTAssertEqual(Telemetry(bytes: s.bytes), x)
            case "CalResult":
                let x = CalResult(kind: UInt8(s.int("kind")), applied: UInt8(s.int("applied")),
                                  rawMin: UInt16(s.int("rawMin")), rawMax: UInt16(s.int("rawMax")))
                XCTAssertEqual(x.serialized(), s.bytes)
                XCTAssertEqual(CalResult(bytes: s.bytes), x)
            case "Peer":
                let x = Peer(deck: UInt8(s.int("deck")), flags: UInt8(s.int("flags")), rssi: Int8(s.int("rssi")),
                             reserved: UInt8(s.int("reserved")), rttUs: UInt16(s.int("rttUs")), offsetUs: Int32(s.int("offsetUs")))
                XCTAssertEqual(x.serialized(), s.bytes)
                XCTAssertEqual(Peer(bytes: s.bytes), x)
            default:
                XCTFail("vector for unknown struct \(s.struct)")
            }
        }
    }

    func testStructsRejectShortInputAndIgnoreTrailingBytes() {
        let t = Telemetry(seq: 1, jogTotal: Int32.min, touchRaw: [1, 2, 3, 4, 5, 6, 7, 8])
        let bytes = t.serialized()
        XCTAssertEqual(bytes.count, Telemetry.byteCount)
        XCTAssertNil(Telemetry(bytes: Array(bytes.dropLast())))
        XCTAssertEqual(Telemetry(bytes: bytes + [0xAA, 0xBB]), t, "newer firmware may append fields")
        XCTAssertNil(Info(bytes: [UInt8](repeating: 0, count: Info.byteCount - 1)))
        XCTAssertNil(CalResult(bytes: []))
        XCTAssertNil(Peer(bytes: [UInt8](repeating: 0, count: Peer.byteCount - 1)))
        XCTAssertEqual(Peer(bytes: Peer(deck: 1, rssi: Int8.min, offsetUs: Int32.min).serialized())?.rssi, Int8.min)
    }

    func testCc14Vectors() {
        XCTAssertFalse(v.cc14.isEmpty)
        for c in v.cc14 {
            let value = UInt16(c.value14)
            let (msb, lsb) = MidiEncoding.split14(value)
            XCTAssertEqual(msb, c.msb)
            XCTAssertEqual(lsb, c.lsb)
            XCTAssertEqual(MidiEncoding.join14(msb: c.msb, lsb: c.lsb), value)

            var asm = Cc14Assembler()
            let updates = MidiEvent.cc14(.bpm, channel: 0, value: value).compactMap { e -> Cc14Assembler.Update? in
                guard case let .controlChange(ch, cc, val) = e else { return nil }
                return asm.feed(channel: ch, controller: cc, value: val)
            }
            XCTAssertEqual(updates.last?.value, value, "value14 \(c.value14)")
        }
    }

    func testRelativeVectors() {
        XCTAssertFalse(v.relative.isEmpty)
        for r in v.relative {
            XCTAssertEqual(MidiEncoding.encodeRelative(r.delta), r.value, "delta \(r.delta)")
            if abs(r.delta) <= 63 { XCTAssertEqual(MidiEncoding.decodeRelative(r.value), r.delta) }
        }
        XCTAssertEqual(MidiEncoding.decodeRelative(0), -64, "value 0 is never sent, but is -64 if it arrives")
    }

    func testUsbMidiVectors() {
        XCTAssertFalse(v.usbMidi.isEmpty)
        for u in v.usbMidi {
            let flat = u.packets.flatMap { $0 }
            XCTAssertEqual(UsbMidi.encode(cable: u.cable, message: u.message), flat, u.name)

            let decoded = UsbMidi.decode(flat)
            XCTAssertEqual(decoded.count, u.packets.count, u.name)
            XCTAssertTrue(decoded.allSatisfy { $0.cable == u.cable }, u.name)
            XCTAssertEqual(decoded.flatMap(\.bytes), u.message, u.name)
            XCTAssertEqual(decoded.map(\.cin), u.packets.map { $0[0] & 0x0F }, u.name)
        }
    }

    func testUsbMidiVectorsThroughTheEventParser() {
        for u in v.usbMidi {
            var parser = MidiStreamParser()
            let events = parser.feed(UsbMidi.decode(u.packets.flatMap { $0 }).flatMap(\.bytes))
            XCTAssertEqual(events.count, 1, u.name)
            XCTAssertEqual(events.first?.bytes, u.message, "\(u.name): event must serialize back to the same message")
        }
    }

    func testMtcVectors() {
        XCTAssertFalse(v.mtc.isEmpty)
        for m in v.mtc {
            let rate = MtcFrameRate(rawValue: UInt8(m.fpsType))!
            let expected = Timecode(hours: m.h, minutes: m.m, seconds: m.s, frames: m.f, rate: rate)

            var parser = MidiStreamParser()
            var asm = MtcAssembler()
            var assembled: [Timecode] = []
            for qf in m.quarterFrames {
                for case let .mtcQuarterFrame(piece, nibble) in parser.feed(qf) {
                    if let tc = asm.feed(piece: piece, nibble: nibble) { assembled.append(tc) }
                }
            }
            XCTAssertEqual(assembled, [expected])
            XCTAssertEqual(Timecode(fullFrame: m.fullFrame), expected)
        }
    }

    func testClockPeriodVectors() {
        XCTAssertFalse(v.clockPeriod.isEmpty)
        for c in v.clockPeriod {
            let us = MidiClock.periodMicroseconds(bpmX10: c.bpmX10)
            XCTAssertEqual(us, c.periodUs, accuracy: 1e-6)
            XCTAssertEqual(MidiClock.bpmX10(periodMicroseconds: us), Double(c.bpmX10), accuracy: 1e-9)
        }
    }

    // MARK: drift against the contract JSON

    func testConfigTableMatchesContract() {
        let keys = ContractFile.shared.config.keys
        XCTAssertEqual(ConfigTable.all.count, keys.count)
        XCTAssertEqual(ConfigKey.allCases.count, keys.count)
        for (i, k) in keys.enumerated() {
            let d = ConfigTable.all[i]
            XCTAssertEqual(Int(d.key.rawValue), k.id)
            XCTAssertEqual(d.name, k.name)
            XCTAssertEqual(Int(d.minValue), k.min, k.name)
            XCTAssertEqual(Int(d.maxValue), k.max, k.name)
            XCTAssertEqual(Int(d.defaultValue), k.default, k.name)
            XCTAssertTrue(d.minValue <= d.defaultValue && d.defaultValue <= d.maxValue, k.name)
        }
    }

    func testCommandReplyAndErrorCodesMatchContract() {
        let s = ContractFile.shared.sysex
        XCTAssertEqual(SysExCmd.allCases.map { Int($0.rawValue) }, s.commands.map(\.id))
        XCTAssertEqual(SysExReply.allCases.map { Int($0.rawValue) }, s.replies.map(\.id))
        XCTAssertEqual(SysExError.allCases.map { Int($0.rawValue) }, s.errors.map(\.id))
    }

    func testControl14TableMatchesContract() {
        let byId = Dictionary(uniqueKeysWithValues: ContractFile.shared.midi.controls.map { ($0.id, $0) })
        let names: [Control14: String] = [.crossfader: "crossfader", .channelVolume: "channelVolume", .pitch: "pitch",
                                          .knob: "knob", .bpm: "bpm"]
        XCTAssertEqual(names.count, Control14.allCases.count)
        for (control, id) in names {
            XCTAssertEqual(Int(control.msb), byId[id]?.number, id)
            XCTAssertEqual(Int(control.lsb), byId[id]?.lsb, id)
        }
        XCTAssertEqual(Int(Proto.CC.jog), byId["jog"]?.number)
        XCTAssertEqual(Int(Proto.CC.bpmNudge), byId["bpmNudge"]?.number)
        XCTAssertEqual(Int(Proto.CC.transport), byId["transport"]?.number)
        XCTAssertEqual(Int(Proto.Note.cue), byId["cue"]?.number)
        XCTAssertEqual(Int(Proto.Note.rev), byId["rev"]?.number)
        XCTAssertEqual(Int(Proto.Note.touch), byId["touch"]?.number)
        XCTAssertEqual(Int(Proto.Note.play), byId["play"]?.number)
        XCTAssertEqual(Int(Proto.Note.tap), byId["tap"]?.number)
    }

    func testMtcFrameRatesMatchContract() {
        let types = ContractFile.shared.midi.mtcFpsTypes
        XCTAssertEqual(MtcFrameRate.allCases.map { Int($0.rawValue) }, types.map(\.id))
        for t in types { XCTAssertEqual(MtcFrameRate(rawValue: UInt8(t.id))?.framesPerSecond, t.fps) }
    }
}

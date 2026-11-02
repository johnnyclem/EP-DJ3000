import XCTest
@testable import EPDJ3000Protocol

final class MidiParserTests: XCTestCase {
    func testChannelMessages() {
        var p = MidiStreamParser()
        XCTAssertEqual(p.feed([0x90, 36, 127, 0x80, 36, 0, 0xB1, 22, 67]), [
            .noteOn(channel: 0, note: 36, velocity: 127),
            .noteOff(channel: 0, note: 36, velocity: 0),
            .controlChange(channel: 1, controller: 22, value: 67),
        ])
    }

    func testNoteOnWithVelocityZeroIsNoteOff() {
        var p = MidiStreamParser()
        XCTAssertEqual(p.feed([0x91, 38, 0]), [.noteOff(channel: 1, note: 38, velocity: 0)])
    }

    func testRunningStatus() {
        var p = MidiStreamParser()
        XCTAssertEqual(p.feed([0xB0, 22, 65, 22, 63, 23, 10]), [
            .controlChange(channel: 0, controller: 22, value: 65),
            .controlChange(channel: 0, controller: 22, value: 63),
            .controlChange(channel: 0, controller: 23, value: 10),
        ])
    }

    func testRunningStatusSurvivesRealtimeButNotSystemCommon() {
        var p = MidiStreamParser()
        XCTAssertEqual(p.feed([0xB0, 22, 65, 0xF8, 22, 66]), [
            .controlChange(channel: 0, controller: 22, value: 65), .clock,
            .controlChange(channel: 0, controller: 22, value: 66),
        ])
        XCTAssertEqual(p.feed([0xF1, 0x00, 22, 67]), [.mtcQuarterFrame(piece: 0, nibble: 0)], "data after system common has no status")
    }

    func testRealtimeInsideAMessageDoesNotBreakIt() {
        var p = MidiStreamParser()
        XCTAssertEqual(p.feed([0x90, 0xF8, 36, 0xFA, 127]), [.clock, .start, .noteOn(channel: 0, note: 36, velocity: 127)])
    }

    func testSplitDataBytesAcrossFeeds() {
        var p = MidiStreamParser()
        XCTAssertEqual(p.feed([0x90]), [])
        XCTAssertEqual(p.feed([36]), [])
        XCTAssertEqual(p.feed([127]), [.noteOn(channel: 0, note: 36, velocity: 127)])
    }

    func testRealtimeAndSystemCommon() {
        var p = MidiStreamParser()
        XCTAssertEqual(p.feed([0xF8, 0xFA, 0xFB, 0xFC, 0xFE, 0xF9, 0xFD, 0xFF]), [
            .clock, .start, .continue, .stop, .other(status: 0xFE, data: []), .other(status: 0xFF, data: []),
        ])
        XCTAssertEqual(p.feed([0xF2, 0x06, 0x00]), [.songPosition(beats: 6)])
        XCTAssertEqual(p.feed([0xF2, 0x7F, 0x7F]), [.songPosition(beats: 16383)])
        XCTAssertEqual(p.feed([0xF2, 0x01, 0x01]), [.songPosition(beats: 129)], "LSB first")
        XCTAssertEqual(p.feed([0xF1, 0x76]), [.mtcQuarterFrame(piece: 7, nibble: 6)])
        XCTAssertEqual(p.feed([0xF3, 4]), [.other(status: 0xF3, data: [4])])
        XCTAssertEqual(p.feed([0xF6]), [.other(status: 0xF6, data: [])])
        XCTAssertEqual(p.feed([0xF4, 0xF5]), [], "undefined")
    }

    func testOtherChannelMessagesKeepStatusAndData() {
        var p = MidiStreamParser()
        XCTAssertEqual(p.feed([0xC3, 5, 0xE1, 0, 64, 0xD0, 9, 0xA2, 60, 30]), [
            .other(status: 0xC3, data: [5]), .other(status: 0xE1, data: [0, 64]),
            .other(status: 0xD0, data: [9]), .other(status: 0xA2, data: [60, 30]),
        ])
    }

    func testSysExSplitAcrossFeedsAndWithRealtimeInside() {
        var p = MidiStreamParser()
        XCTAssertEqual(p.feed([0xF0, 0x7D, 0x45]), [])
        XCTAssertEqual(p.feed([0xF8, 0x44, 0xFA, 1]), [.clock, .start])
        XCTAssertEqual(p.feed([0xF7]), [.sysex([0xF0, 0x7D, 0x45, 0x44, 1, 0xF7])])
    }

    func testStatusByteAbortsSysEx() {
        var p = MidiStreamParser()
        XCTAssertEqual(p.feed([0xF0, 1, 2, 0x90, 36, 127, 0xF7]), [.noteOn(channel: 0, note: 36, velocity: 127)])
        XCTAssertEqual(p.feed([0xF0, 1, 2, 0xF0, 3, 0xF7]), [.sysex([0xF0, 3, 0xF7])], "a second F0 restarts")
    }

    func testStrayBytesAreIgnored() {
        var p = MidiStreamParser()
        XCTAssertEqual(p.feed([0x10, 0x20, 0xF7, 0x30]), [])
        XCTAssertEqual(p.feed([0x90, 36, 127]), [.noteOn(channel: 0, note: 36, velocity: 127)])
    }

    func testOversizedSysExIsDroppedAndParsingResynchronises() {
        var p = MidiStreamParser(maxSysExLength: 8)
        XCTAssertEqual(p.feed([0xF0] + [UInt8](repeating: 1, count: 20) + [0xF7]), [])
        XCTAssertEqual(p.feed([0xF0, 1, 2, 0xF7]), [.sysex([0xF0, 1, 2, 0xF7])])
    }

    func testResetDropsPartialMessage() {
        var p = MidiStreamParser()
        _ = p.feed([0xF0, 1, 2])
        p.reset()
        XCTAssertEqual(p.feed([0xF7]), [])
        XCTAssertEqual(p.feed([36, 127]), [], "no status after reset")
    }

    func testEveryEventSerialisesToBytesThatParseBack() {
        let events: [MidiEvent] = [
            .noteOn(channel: 3, note: 39, velocity: 127), .noteOff(channel: 15, note: 0, velocity: 64),
            .controlChange(channel: 1, controller: 29, value: 2), .clock, .start, .continue, .stop,
            .songPosition(beats: 12345), .mtcQuarterFrame(piece: 5, nibble: 9),
            .sysex([0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x61, 2, 3, 4, 0xF7]),
            .other(status: 0xC2, data: [7]), .other(status: 0xE0, data: [1, 2]), .other(status: 0xFE, data: []),
        ]
        var p = MidiStreamParser()
        XCTAssertEqual(p.feed(events.flatMap(\.bytes)), events)
    }

    func testChannelAccessor() {
        XCTAssertEqual(MidiEvent.noteOn(channel: 4, note: 0, velocity: 1).channel, 4)
        XCTAssertEqual(MidiEvent.other(status: 0xC5, data: [0]).channel, 5)
        XCTAssertNil(MidiEvent.clock.channel)
        XCTAssertNil(MidiEvent.other(status: 0xF6, data: []).channel)
        XCTAssertNil(MidiEvent.sysex([0xF0, 0xF7]).channel)
    }

    func testBytesMaskOutOfRangeValues() {
        XCTAssertEqual(MidiEvent.controlChange(channel: 0x1F, controller: 0xFF, value: 0x80).bytes, [0xBF, 0x7F, 0x00])
    }
}

final class MidiHelpersTests: XCTestCase {
    func testJogBuilderUsesOffset64() {
        XCTAssertEqual(MidiEvent.jog(delta: 5, channel: 1), .controlChange(channel: 1, controller: 22, value: 69))
        XCTAssertEqual(MidiEvent.jog(delta: -200, channel: 0), .controlChange(channel: 0, controller: 22, value: 1))
    }

    func testCc14BuilderSendsMsbThenLsb() {
        XCTAssertEqual(MidiEvent.cc14(.crossfader, channel: 0, value: 16383), [
            .controlChange(channel: 0, controller: 23, value: 127), .controlChange(channel: 0, controller: 55, value: 127)])
        XCTAssertEqual(MidiEvent.cc14(.bpm, channel: 1, value: 1285), [
            .controlChange(channel: 1, controller: 20, value: 10), .controlChange(channel: 1, controller: 52, value: 5)])
    }

    func testCc14AssemblerAcceptsEitherHalfFirst() {
        var msbFirst = Cc14Assembler()
        XCTAssertEqual(msbFirst.feed(channel: 0, controller: 20, value: 10)?.value, 10 << 7)
        XCTAssertEqual(msbFirst.feed(channel: 0, controller: 52, value: 5)?.value, 1285)

        var lsbFirst = Cc14Assembler()
        XCTAssertEqual(lsbFirst.feed(channel: 0, controller: 52, value: 5)?.value, 5)
        XCTAssertEqual(lsbFirst.feed(channel: 0, controller: 20, value: 10)?.value, 1285)
    }

    func testCc14AssemblerSkippedHalfKeepsItsLastValue() {
        var a = Cc14Assembler()
        _ = a.feed(channel: 0, controller: 23, value: 10)
        _ = a.feed(channel: 0, controller: 55, value: 100)
        XCTAssertEqual(a.feed(channel: 0, controller: 23, value: 11)?.value, (11 << 7) | 100, "LSB unchanged and skipped")
        XCTAssertEqual(a.feed(channel: 0, controller: 55, value: 3)?.value, (11 << 7) | 3, "MSB unchanged and skipped")
    }

    func testCc14AssemblerSuppressesRepeatsAndSeparatesControlsAndChannels() {
        var a = Cc14Assembler()
        XCTAssertNotNil(a.feed(channel: 0, controller: 23, value: 0), "first message is always news, even at 0")
        XCTAssertNil(a.feed(channel: 0, controller: 23, value: 0))
        XCTAssertNil(a.feed(channel: 0, controller: 55, value: 0))
        XCTAssertEqual(a.feed(channel: 1, controller: 23, value: 7)?.channel, 1)
        XCTAssertEqual(a.feed(channel: 0, controller: 24, value: 7)?.control, .knob)
        XCTAssertEqual(a.feed(channel: 0, controller: 23, value: 9)?.value, 9 << 7, "channel 1 and the knob did not disturb it")
    }

    func testCc14AssemblerObserveReportsRepeatsAndSaysWhetherTheValueChanged() {
        var a = Cc14Assembler()
        var seen = [Bool]()
        func observe(_ cc: UInt8, _ value: UInt8) -> Cc14Assembler.Update? {
            guard let (u, changed) = a.observe(channel: 0, controller: cc, value: value) else { return nil }
            seen.append(changed)
            return u
        }
        XCTAssertEqual(observe(23, 10)?.value, 10 << 7)
        XCTAssertEqual(observe(55, 5)?.value, (10 << 7) | 5)
        XCTAssertEqual(observe(23, 10)?.value, (10 << 7) | 5, "a repeat still reports the pair's value")
        XCTAssertEqual(observe(55, 5)?.control, .crossfader)
        XCTAssertEqual(seen, [true, true, false, false])
        // feed is the changes-only view of the same stream
        XCTAssertNil(a.feed(channel: 0, controller: 23, value: 10))
        XCTAssertEqual(a.feed(channel: 0, controller: 23, value: 11)?.value, (11 << 7) | 5)
        XCTAssertNil(observe(Proto.CC.jog, 5), "not a pair")
        XCTAssertEqual(seen.count, 4)
    }

    func testCc14AssemblerIgnoresControllersThatAreNotPairs() {
        var a = Cc14Assembler()
        for cc in [Proto.CC.jog, Proto.CC.bpmNudge, Proto.CC.transport, 1, 64] {
            XCTAssertNil(a.feed(channel: 0, controller: cc, value: 5), "CC \(cc)")
        }
    }

    func testMtcAssemblerNeedsAnInOrderRunStartingAtPieceZero() {
        var asm = MtcAssembler()
        for piece: UInt8 in 3...7 { XCTAssertNil(asm.feed(piece: piece, nibble: 0), "joined mid-cycle") }
        // A clean run afterwards works: 12:34:56:07 at 30 fps.
        let nibbles: [UInt8] = [7, 0, 8, 3, 2, 2, 12, 3 << 1]
        var result: Timecode?
        for (i, n) in nibbles.enumerated() { result = asm.feed(piece: UInt8(i), nibble: n) ?? result }
        XCTAssertEqual(result, Timecode(hours: 12, minutes: 34, seconds: 56, frames: 7, rate: .fps30))
        XCTAssertEqual(result?.description, "12:34:56:07")
    }

    func testMtcAssemblerRestartsOnPieceZeroAndDropsAfterAGap() {
        var asm = MtcAssembler()
        _ = asm.feed(piece: 0, nibble: 1)
        _ = asm.feed(piece: 1, nibble: 0)
        XCTAssertNil(asm.feed(piece: 3, nibble: 0), "piece 2 was lost")
        for piece: UInt8 in 4...7 { XCTAssertNil(asm.feed(piece: piece, nibble: 0)) }
        var last: Timecode?
        for piece: UInt8 in 0...7 { last = asm.feed(piece: piece, nibble: 0) ?? last }
        XCTAssertEqual(last, Timecode(hours: 0, minutes: 0, seconds: 0, frames: 0, rate: .fps24))
    }

    func testFullFrameParsing() {
        XCTAssertNil(Timecode(fullFrame: [0xF0, 0x7F, 0x7F, 0x01, 0x02, 0x61, 2, 3, 4, 0xF7]), "not a full frame (sub-id 2)")
        XCTAssertNil(Timecode(fullFrame: [0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x61, 2, 3, 0xF7]), "short")
        XCTAssertEqual(Timecode(fullFrame: [0xF0, 0x7F, 0x05, 0x01, 0x01, 0x61, 2, 3, 4, 0xF7])?.hours, 1, "any device id")
    }

    func testClockMathExamples() {
        XCTAssertEqual(MidiClock.periodMicroseconds(bpmX10: 1200), 20833.333333, accuracy: 1e-3)
        XCTAssertEqual(MidiClock.bpmX10(periodMicroseconds: 20833.3333333), 1200, accuracy: 1e-3)
    }
}

import Foundation
import XCTest
@testable import EPDJ3000KitPure
import EPDJ3000Protocol

/// DeckSession feeds CoreMIDI packets (arbitrary chunks of bytes) into MidiStreamParser. These pin the chunking behaviour
/// it relies on; the telemetry check goes through the Kit's own classifier.
final class PacketChunkingTests: XCTestCase {
    private func events(_ chunks: [[UInt8]]) -> [MidiEvent] {
        var parser = MidiStreamParser()
        return chunks.flatMap { parser.feed($0) }
    }

    func testSysExSplitAtEveryPositionStillLooksLikeTelemetryToTheMonitor() {
        let frame = SysExFrame(deck: 0, reply: .telemetry, payload: Telemetry(seq: 9, jogTotal: -5).serialized()).encode()
        for cut in 1..<frame.count {
            let got = events([Array(frame[..<cut]), Array(frame[cut...])])
            XCTAssertEqual(got, [.sysex(frame)], "cut at \(cut)")
            // DeckSession drops telemetry from the log by asking exactly this of the reassembled bytes.
            if case let .sysex(bytes)? = got.first { XCTAssertTrue(MidiFormat.isTelemetryFrame(bytes), "cut at \(cut)") }
        }
    }

    func testSysExInThreeBytePacketsLikeUsbMidi() {
        let frame = SysExFrame(deck: 127, command: .getInfo).encode()
        let chunks = stride(from: 0, to: frame.count, by: 3).map { Array(frame[$0..<min($0 + 3, frame.count)]) }
        XCTAssertEqual(events(chunks), [.sysex(frame)])
    }

    func testRealTimeBytesInsideASplitSysEx() {
        let frame = SysExFrame(deck: 127, command: .getInfo).encode()
        let half = frame.count / 2
        let got = events([Array(frame[..<half]) + [0xF8], [0xF8] + Array(frame[half...])])
        XCTAssertEqual(got, [.clock, .clock, .sysex(frame)])
    }

    func testSeveralMessagesInOnePacketAndRunningStatus() {
        let got = events([[0xB0, 22, 66, 22, 63, 0x90, 38, 127]])
        XCTAssertEqual(got, [.controlChange(channel: 0, controller: 22, value: 66),
                             .controlChange(channel: 0, controller: 22, value: 63),
                             .noteOn(channel: 0, note: 38, velocity: 127)])
    }

    func testMessageSplitAcrossPackets() {
        XCTAssertEqual(events([[0x90, 36], [127]]), [.noteOn(channel: 0, note: 36, velocity: 127)])
    }
}

// Pure-Swift reproducers for disagreements the end-to-end suites found, so they stay visible (and are promoted to
// plain assertions the moment they are fixed) even on a machine without the simulator. See docs/E2E.md "Findings".

import EPDJ3000Protocol
import XCTest

final class KnownIssueReproducers: XCTestCase {
    /// E2E-2: the unit re-sends a fader pair when its role changes (it must tell the host what the pot means now), but
    /// ControllerState only notices a CHANGED 14-bit value, so switching back to a pair it already knows, with the pot
    /// where it was, leaves faderRole on the previous role.
    func testE2E2_RoleSwitchBackAtAnUnchangedPositionIsNotSeen() {
        var state = ControllerState(channel: 0)
        state.apply(MidiEvent.cc14(.channelVolume, channel: 0, value: 16383))
        XCTAssertEqual(state.faderRole, .channelVolume)
        state.apply(MidiEvent.cc14(.pitch, channel: 0, value: 16383))
        XCTAssertEqual(state.faderRole, .pitch)
        state.apply(MidiEvent.cc14(.channelVolume, channel: 0, value: 16383))
        knownIssue("E2E-2", "ControllerState.faderRole is \(String(describing: state.faderRole)) after the unit sent the CHANNEL_VOLUME pair again",
                   holds: state.faderRole == .channelVolume)
    }
}

import Foundation
import XCTest
@testable import EPDJ3000KitPure
import EPDJ3000Protocol

final class RigChecklistTests: XCTestCase {
    private func unit(_ deck: Deck, caps: Caps = [.dac, .usbAudio, .link], peers: [Peer]? = nil) -> RigUnit {
        RigUnit(name: "EP-DJ3000 Deck \(deck == .a ? "A" : "B")", deck: deck, caps: caps, peers: peers)
    }

    private func check(_ id: String, _ checks: [RigCheck]) -> RigCheck { checks.first { $0.id == id }! }

    func testNothingConnected() {
        let c = RigChecklist.evaluate(units: [], audioDeviceNames: [])
        XCTAssertEqual(check("decks", c).status, .problem)
        XCTAssertEqual(check("usbAudio", c).status, .info)
    }

    func testHealthyRig() {
        let peerOfA = Peer(deck: 1, flags: 1, rssi: -50, rttUs: 900)
        let peerOfB = Peer(deck: 0, flags: 1, rssi: -52, rttUs: 900)
        let c = RigChecklist.evaluate(units: [unit(.a, peers: [peerOfA]), unit(.b, peers: [peerOfB])],
                                      audioDeviceNames: ["MacBook Pro Speakers", "EP-DJ3000 Deck A", "EP-DJ3000 Deck B"])
        XCTAssertEqual(check("decks", c).status, .ok)
        XCTAssertEqual(check("ids", c).status, .ok)
        XCTAssertEqual(check("link", c).status, .ok)
        XCTAssertEqual(check("dac", c).status, .ok)
        XCTAssertEqual(check("usbAudio", c).status, .ok)
        XCTAssertTrue(check("link", c).detail.contains("Deck B"))
        XCTAssertTrue(check("link", c).detail.contains("-50 dBm"))
    }

    func testTwoUnitsClaimingOneDeck() {
        let c = RigChecklist.evaluate(units: [unit(.a), unit(.a)], audioDeviceNames: [])
        XCTAssertEqual(check("ids", c).status, .problem)
    }

    func testConflictSeenOnlyOverTheRadio() {
        let sameDeckHeard = RigChecklist.evaluate(units: [unit(.a, peers: [Peer(deck: 0)])], audioDeviceNames: [])
        XCTAssertEqual(check("ids", sameDeckHeard).status, .problem, "a peer announcing our own deck id")
    }

    func testPeerStatusConflictBitIsReadThroughPeerStatus() {
        func ids(_ status: UInt8) -> RigCheck {
            check("ids", RigChecklist.evaluate(units: [unit(.a, peers: [Peer(deck: 1, status: status)])], audioDeviceNames: []))
        }
        let flagged = ids(PeerStatus.conflict.rawValue)
        XCTAssertEqual(flagged.status, .problem, "two other units claim one id: only the status bit says so")
        XCTAssertTrue(flagged.detail.contains("Deck A hears another unit claiming the same deck id"), flagged.detail)
        XCTAssertEqual(ids(0).status, .ok)
        XCTAssertEqual(ids(0xFE).status, .ok, "bits 1-7 are not the conflict bit")
        XCTAssertEqual(ids(0xFF).status, .problem)
    }

    func testAConflictOnOnePeerOfSeveralIsStillAConflict() {
        let peers = [Peer(deck: 1), Peer(deck: 1, status: PeerStatus.conflict.rawValue)]
        let c = RigChecklist.evaluate(units: [unit(.a, peers: peers)], audioDeviceNames: [])
        XCTAssertEqual(check("ids", c).status, .problem)
    }

    func testSingleDeckAloneIsNotAFailure() {
        let c = RigChecklist.evaluate(units: [unit(.a, peers: [])], audioDeviceNames: ["EP-DJ3000 Deck A"])
        XCTAssertEqual(check("decks", c).status, .warning)
        XCTAssertEqual(check("link", c).status, .info)
    }

    func testOneCableModeIsRecognised() {
        // Deck B (no host) forwards through Deck A: A's LINK_STATUS shows B with FORWARDING and without USB_HOST.
        let c = RigChecklist.evaluate(units: [unit(.a, peers: [Peer(deck: 1, flags: PeerFlags.forwarding.rawValue, rssi: -40)])],
                                      audioDeviceNames: ["EP-DJ3000 Deck A"])
        XCTAssertEqual(check("link", c).status, .ok)
        XCTAssertTrue(check("link", c).detail.contains("one-cable mode"))
    }

    func testMissingDacAndMissingAudioDevice() {
        let noDac = RigChecklist.evaluate(units: [unit(.a, caps: [.link])], audioDeviceNames: [])
        XCTAssertEqual(check("dac", noDac).status, .warning)
        XCTAssertTrue(check("dac", noDac).detail.contains("Deck A"))
        let notListed = RigChecklist.evaluate(units: [unit(.a), unit(.b)], audioDeviceNames: ["EP-DJ3000 Deck A"])
        XCTAssertEqual(check("usbAudio", notListed).status, .problem)
        XCTAssertTrue(check("usbAudio", notListed).detail.contains("Deck B: enumerates as audio, but macOS shows no"))
        let usbOff = RigChecklist.evaluate(units: [unit(.a, caps: [.dac])], audioDeviceNames: [])
        XCTAssertEqual(check("usbAudio", usbOff).status, .warning)
    }

    func testCapabilitiesNotReadYet() {
        let c = RigChecklist.evaluate(units: [RigUnit(name: "EP-DJ3000", deck: nil)], audioDeviceNames: [])
        XCTAssertEqual(check("dac", c).status, .info)
        XCTAssertEqual(check("link", c).status, .info)
    }
}

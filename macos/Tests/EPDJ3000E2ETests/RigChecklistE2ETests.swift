// The Rig tab's pure judgement (EPDJ3000KitPure.RigChecklist) fed with what real units report over SysEx: GET_INFO,
// telemetry flags and LINK_STATUS from the firmware, not hand-built values.

import EPDJ3000Protocol
import EPDJ3000KitPure
import XCTest

final class RigChecklistE2ETests: E2ETestCase {
    private func rigUnit(_ world: SimWorld, unit: Int, name: String) async throws -> RigUnit {
        let c = world.client(unit: unit)
        let info = try await c.client.getInfo()
        let peers = try await c.client.linkStatus()
        try await c.client.startStream(rateHz: 20)
        try await world.run(ms: 100)
        let flags = c.telemetry.take().last.map { StateFlags(rawValue: $0.flags) }
        try await c.client.stopStream()
        return RigUnit(name: name, deck: Deck(rawValue: info.deck), caps: Caps(rawValue: info.caps), flags: flags, peers: peers)
    }

    private func check(_ id: String, _ checks: [RigCheck]) throws -> RigCheck {
        try XCTUnwrap(checks.first { $0.id == id }, "no check \(id)")
    }

    func testTwoDecksOnUSBAreAllGreenOnTheLinkAndTheDeckIds() async throws {
        try await withWorld("units=2 deck0=0 deck1=1") { world in
            try await world.run(ms: 2000)
            let a = try await rigUnit(world, unit: 0, name: "A")
            let b = try await rigUnit(world, unit: 1, name: "B")
            let checks = RigChecklist.evaluate(units: [a, b], audioDeviceNames: [])
            XCTAssertEqual(try check("decks", checks).status, .ok)
            XCTAssertEqual(try check("ids", checks).status, .ok)
            let link = try check("link", checks)
            XCTAssertEqual(link.status, .ok, link.detail)
            XCTAssertTrue(link.detail.contains("Deck A sees Deck B") && link.detail.contains("Deck B sees Deck A"), link.detail)
            XCTAssertFalse(link.detail.contains("one-cable"), "both decks have a host: no bridge")
            XCTAssertTrue(a.flags?.contains(.linkUp) == true && a.flags?.contains(.usbMounted) == true)
            XCTAssertEqual(try check("dac", checks).status, .warning, "the simulated units have no DAC")
        }
    }

    func testAPowerOnlyDeckBIsRecognisedAsRidingOnDeckAsUSB() async throws {
        try await withWorld("units=2 deck0=0 deck1=1 usb1=0") { world in
            try await world.run(ms: 2000)
            let a = try await rigUnit(world, unit: 0, name: "A")  // the only unit the Mac can see
            XCTAssertTrue(a.flags?.contains(.bridging) == true, "telemetry says Deck A is bridging")
            let checks = RigChecklist.evaluate(units: [a], audioDeviceNames: [])
            XCTAssertEqual(try check("decks", checks).status, .warning)
            XCTAssertTrue(try check("decks", checks).detail.contains("one-cable mode"))
            let link = try check("link", checks)
            XCTAssertEqual(link.status, .ok, link.detail)
            let peer = try XCTUnwrap(a.peers?.first)
            let flags = PeerFlags(rawValue: peer.flags)
            XCTAssertFalse(flags.contains(.usbHost), "Deck B has no host")
            // RigChecklist words "riding on this unit's USB" for forwarding && !usbHost
            XCTAssertTrue(flags.contains(.forwarding), "the firmware announces FORWARDING for a deck whose MIDI is being carried")
            XCTAssertTrue(link.detail.contains("one-cable mode"), link.detail)
        }
    }

    func testADuplicateDeckIdIsAProblemFromBothSidesOfTheRadio() async throws {
        try await withWorld("units=2 deck0=0 deck1=0") { world in
            try await world.run(ms: 3000)
            let a = try await rigUnit(world, unit: 0, name: "A")
            let b = try await rigUnit(world, unit: 1, name: "B")
            let both = RigChecklist.evaluate(units: [a, b], audioDeviceNames: [])
            XCTAssertEqual(try check("ids", both).status, .problem)
            // even with only one unit visible, its LINK_STATUS shows the conflict
            let alone = RigChecklist.evaluate(units: [a], audioDeviceNames: [])
            XCTAssertEqual(try check("ids", alone).status, .problem, "Peer.status bit 0 / Peer.deck == Info.deck")
        }
    }

    func testALoneUnitWithNobodyToLinkToIsInformationNotAFailure() async throws {
        try await withWorld("units=1") { world in
            let a = try await rigUnit(world, unit: 0, name: "A")
            let checks = RigChecklist.evaluate(units: [a], audioDeviceNames: [])
            XCTAssertEqual(try check("link", checks).status, .info)
            XCTAssertEqual(try check("ids", checks).status, .ok)
        }
    }
}

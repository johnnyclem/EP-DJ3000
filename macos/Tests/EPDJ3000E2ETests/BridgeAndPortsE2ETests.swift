// Two units on the in-process radio (the "one cable" bridge, LINK_STATUS, deck-id conflicts, rig groups) and the
// other USB-MIDI cables (TRS pass-through, channel filter). The client side is the real DeviceClient / ControllerState.

import EPDJ3000Protocol
import XCTest

final class BridgeAndPortsE2ETests: E2ETestCase {
    private let bridgeArgs = "units=2 deck0=0 deck1=1 usb1=0"

    func testPowerOnlyDeckBRidesOnDeckAsCable2AndItsMotionArrivesExactlyUnderRadioLoss() async throws {
        try await withWorld(bridgeArgs + " loss=0.2 dup=0.05 jitter=400") { world in
            let a = world.client(unit: 0, cable: 0)
            let linked = world.client(unit: 0, cable: 2)  // "Linked deck": Deck B's MIDI, bridged by Deck A
            try await world.run(ms: 1500)  // discovery
            let sim0 = try await world.status(unit: 0)
            XCTAssertTrue(sim0.bool("bridging"), "link up and bridging")
            let deckA = ControllerState(deck: .a)
            var onCable0 = deckA
            var deckB = ControllerState(deck: .b)
            var misrouted = ControllerState(channel: 0)

            try await world.gesture(startRev: 0.25, revs: 1.5, ms: 2700, unit: 1)
            try await world.run(ms: 2000)  // heartbeats repair a lost trailing packet
            for e in linked.events.take() {
                deckB.apply(e)
                misrouted.apply(e)
            }
            onCable0.apply(a.events.take())
            let sim1 = try await world.status(unit: 1)
            XCTAssertEqual(deckB.jogTicks, sim1.jogTotal, "the cumulative jog counter over a lossy radio loses and doubles nothing")
            XCTAssertEqual(Double(deckB.jogTicks), 1536, accuracy: 60)
            XCTAssertEqual(misrouted.jogTicks, 0, "the bridge keeps Deck B's channel")
            XCTAssertEqual(onCable0.jogTicks, 0, "nothing of Deck B appears on Deck A's own cable")
            XCTAssertFalse(deckB.touched)
            let sent = try await world.status(unit: 0)
            observe("bridge at 20% loss + 5% duplication: \(sent.int("airLost")) of \(sent.int("airSent")) packets lost, Deck B counted \(sim1.jogTotal), Deck A's cable 2 carried \(deckB.jogTicks)")
        }
    }

    func testDeckBsButtonsFaderAndTouchCrossTheBridgeAndHostFeedbackComesBack() async throws {
        try await withWorld(bridgeArgs) { world in
            let linked = world.client(unit: 0, cable: 2)
            try await world.run(ms: 1500)
            var deckB = ControllerState(deck: .b)
            _ = try await world.command("buttons u=1 cue=1")
            _ = try await world.command("fader u=1 raw=3000 noise=0")
            try await world.finger(rev: 0.3, pressure: 1, unit: 1)
            try await world.run(ms: 300)
            deckB.apply(linked.events.take())
            XCTAssertEqual(deckB.buttons, [.cue])
            XCTAssertTrue(deckB.touched)
            XCTAssertEqual(deckB.faderRole, .pitch, "Deck B's factory slide pot is the tempo fader too (contract v1.2)")
            let expected = (Double(3000 - 40) / Double(4055 - 40) - 0.015) / 0.97 * 16383
            XCTAssertEqual(Double(deckB.fader), expected, accuracy: 30)

            // Host LED feedback for Deck B goes out of cable 2, over the radio, into Deck B.
            try await linked.client.send([.noteOn(channel: 1, note: Proto.Note.cue, velocity: 127)])
            try await world.run(ms: 200)
            let b = try await world.status(unit: 1)
            XCTAssertTrue(b.led.cueLit, "Deck B's cue LED lit through the bridge")
            let a = try await world.status(unit: 0)
            XCTAssertFalse(a.led.cueLit)
        }
    }

    func testLinkStatusReportsThePeerItsClockOffsetAndALinkGroupSplitsTheRig() async throws {
        try await withWorld("units=2 deck0=0 deck1=1 offset1=-123456") { world in
            let a = world.client(unit: 0, deck: 0).client
            let b = world.client(unit: 1, deck: 1).client
            try await world.run(ms: 6000)  // time sync converges in ~3 s
            let peers = try await a.linkStatus()
            XCTAssertEqual(peers.count, 1)
            let p = try XCTUnwrap(peers.first)
            XCTAssertEqual(p.deck, 1)
            XCTAssertEqual(PeerFlags(rawValue: p.flags).contains(.usbHost), true, "Deck B has a host of its own")
            XCTAssertEqual(PeerFlags(rawValue: p.flags).contains(.isMaster), false, "Deck A is the master, so the flag is on the peer only when the peer is")
            XCTAssertEqual(PeerStatus(rawValue: p.status), [], "no deck-id conflict")
            XCTAssertEqual(p.rssi, -40)
            XCTAssertGreaterThan(p.rttUs, 0)
            XCTAssertLessThan(p.rttUs, 4000)
            XCTAssertEqual(Double(p.offsetUs), -123_456, accuracy: 300, "the partner's clock minus ours (negative here)")
            observe("LINK_STATUS from Deck A: rtt \(p.rttUs) us, offset \(p.offsetUs) us (set: -123456)")

            let mine = try await b.linkStatus()
            let q = try XCTUnwrap(mine.first)
            XCTAssertEqual(q.deck, 0)
            XCTAssertEqual(PeerFlags(rawValue: q.flags).contains(.isMaster), true, "from Deck B's side the peer (Deck A) is the master")
            XCTAssertEqual(Double(q.offsetUs), 123_456, accuracy: 300)

            // A rig id of its own: moving ONE unit to group 5 drops the link; moving the other one brings it back.
            try await a.setConfig(.linkGroup, 5)
            try await world.run(ms: 2500)
            try await expectEqual(try await a.linkStatus().count, 0, "Deck A hears nobody in group 5")
            try await b.setConfig(.linkGroup, 5)
            try await world.run(ms: 3000)
            try await expectEqual(try await a.linkStatus().count, 1, "both in group 5: linked again")
            try await a.loadDefaults()  // LOAD_DEFAULTS resets linkGroup to 0
            try await expectEqual(try await a.getConfig(.linkGroup), 0)
            try await world.run(ms: 2500)
            try await expectEqual(try await a.linkStatus().count, 0, "and the group changed live, without a reboot")
        }
    }

    func testTwoUnitsClaimingTheSameDeckAreReportedAsAConflictNotSilentlyMerged() async throws {
        try await withWorld("units=2 deck0=0 deck1=0") { world in
            let a = world.client(unit: 0).client
            try await world.run(ms: 3000)
            let peers = try await a.linkStatus()
            let p = try XCTUnwrap(peers.first)
            XCTAssertEqual(p.deck, 0, "the peer announces our own deck id")
            XCTAssertEqual(PeerStatus(rawValue: p.status), [.conflict], "Peer.status bit 0 (contract v1.1)")
            let info = try await a.getInfo()
            XCTAssertEqual(p.deck, info.deck, "a host also sees the conflict as Peer.deck == Info.deck")
            let sim = try await world.status(unit: 0)
            XCTAssertTrue(sim.bool("conflict"))
            XCTAssertEqual(sim.led.int("link"), 4, "the status LED shows Conflict")
        }
    }

    func testSetDeckOnOneUnitOfARigRestartsItWithANewEpochAndTheLinkRecovers() async throws {
        try await withWorld("units=2 deck0=0 deck1=0") { world in
            let a = world.client(unit: 0).client
            let b = world.client(unit: 1).client
            try await world.run(ms: 2000)
            try await b.setDeck(.b)  // resolves the conflict the way the app tells the user to
            try await world.run(ms: 4000)
            let peers = try await a.linkStatus()
            let p = try XCTUnwrap(peers.first)
            XCTAssertEqual(p.deck, 1)
            XCTAssertEqual(PeerStatus(rawValue: p.status), [], "conflict gone")
            let bInfo = try await b.getInfo()
            XCTAssertEqual(bInfo.deck, 1)
        }
    }

    // MARK: the other USB-MIDI cables

    func testTrsPortPassesBytesBothWaysAndSeparatesTheCables() async throws {
        try await withWorld { world in
            let trsHost = world.client(unit: 0, cable: 1)
            let mark = world.process.log.count

            // Gear on the TRS jack (an EP-133 playing a note and sending clock) shows up on USB cable 1, not on cable 0.
            try await world.midi([0x99, 36, 100, 0xF8, 0x89, 36, 0], unit: 0, port: "trs")
            try await world.run(ms: 10)
            let onCable1 = world.events(unit: 0, cable: 1, from: mark).flatMap(\.bytes)
            XCTAssertEqual(onCable1, [0x99, 36, 100, 0xF8, 0x89, 36, 0])
            XCTAssertTrue(world.events(unit: 0, cable: 0, from: mark).allSatisfy { $0.bytes.first != 0x99 }, "cable 0 stays the deck's own")
            XCTAssertGreaterThan(trsHost.events.total, 0)

            // The host writing to USB cable 1 comes out of the TRS jack.
            let before = world.process.log.count
            try await trsHost.client.send([.noteOn(channel: 9, note: 40, velocity: 90), .noteOff(channel: 9, note: 40, velocity: 0)])
            try await world.run(ms: 10)
            let toTrs = world.process.log.dropFirst(before).filter { $0.ports & 2 != 0 && $0.kind == "bytes" }.flatMap(\.bytes)
            XCTAssertEqual(toTrs, [0x99, 40, 90, 0x89, 40, 0])

            // With trsMidiEnable = 0 the jack is dead in both directions.
            let c = world.client(unit: 0, cable: 0)
            try await c.client.setConfig(.trsMidiEnable, 0)
            let before2 = world.process.log.count
            try await world.midi([0x99, 41, 100], unit: 0, port: "trs")
            try await world.run(ms: 10)
            XCTAssertTrue(world.events(unit: 0, cable: 1, from: before2).isEmpty)
        }
    }

    func testUnitIgnoresChannelVoiceMessagesForOtherChannelsOnEveryPort() async throws {
        try await withWorld { world in
            // Deck A listens on channel 1 only; Deck B's feedback on any cable must not touch it.
            for cable in ["0", "2"] {
                try await world.midi([0x91, Proto.Note.cue, 127], unit: 0, port: cable)
                try await world.run(ms: 5)
                let led = try await world.status().led
                XCTAssertFalse(led.cueLit, "channel 2 note on cable \(cable)")
            }
            try await world.midi([0x90, Proto.Note.cue, 127], unit: 0, port: "0")
            try await world.run(ms: 5)
            let on = try await world.status().led
            XCTAssertTrue(on.cueLit, "its own channel works")
            // midiChannel override: the unit now answers channel 5 and nothing else
            let c = world.client()
            try await c.client.setConfig(.midiChannel, 4)
            try await world.midi([0x90, Proto.Note.cue, 0], unit: 0, port: "0")  // channel 1 off: ignored now
            try await world.run(ms: 5)
            let stillOn = try await world.status().led
            XCTAssertTrue(stillOn.cueLit, "channel 1 is no longer this unit's channel")
            try await world.midi([0x94, Proto.Note.cue, 0], unit: 0, port: "0")
            try await world.run(ms: 5)
            let off = try await world.status().led
            XCTAssertFalse(off.cueLit)
            // and its own output moved to channel 5
            let mark = world.process.log.count
            _ = try await world.command("fader u=0 raw=3500 noise=0")
            try await world.run(ms: 50)
            let channels = Set(world.midiEvents(from: mark).compactMap { $0.event.channel })
            XCTAssertEqual(channels, [4])
        }
    }

    func testHostCanDriveTheTransportCCButThePlayNoteIsFeedbackOnly() async throws {
        try await withWorld { world in
            let c = world.client()
            var state = ControllerState(deck: .a)
            try await c.client.setConfig(.mode, Int32(Mode.syncMaster.rawValue))
            try await world.run(ms: 100)
            _ = c.events.take()

            // PROTOCOL.md 2.6: the play note never starts a master's clock...
            try await c.client.send([.noteOn(channel: 0, note: Proto.Note.play, velocity: 127)])
            try await world.run(ms: 100)
            state.apply(c.events.take())
            let afterNote = try await world.status()
            XCTAssertEqual(afterNote.transport, Int(TransportState.stopped.rawValue))
            XCTAssertEqual(state.transport, .stopped)

            // ...the transport CC does.
            try await c.client.send([.controlChange(channel: 0, controller: Proto.CC.transport, value: TransportState.playing.rawValue)])
            try await world.run(ms: 100)
            state.apply(c.events.take())
            XCTAssertEqual(state.transport, .playing)
            try await expectEqual(try await world.status().transport, Int(TransportState.playing.rawValue))

            // Paused inside the first beat the position rounds down to 0, and Stop + SPP 0 means "stopped" to a follower
            // (PROTOCOL.md 2.6 edge semantics).
            try await c.client.send([.controlChange(channel: 0, controller: Proto.CC.transport, value: TransportState.paused.rawValue)])
            try await world.run(ms: 100)
            state.apply(c.events.take())
            XCTAssertEqual(state.transport, .stopped, "pause-at-zero is indistinguishable from stop-and-rewind on the wire")
            observe("pause inside the first beat: the follower folds STOPPED, the unit itself reports transport \(try await world.status().transport) (0 stopped, 2 paused)")

            // Pausing later is a real pause, and Continue resumes from the announced beat.
            try await c.client.send([.controlChange(channel: 0, controller: Proto.CC.transport, value: TransportState.playing.rawValue)])
            try await world.run(ms: 1100)
            try await c.client.send([.controlChange(channel: 0, controller: Proto.CC.transport, value: TransportState.paused.rawValue)])
            try await world.run(ms: 100)
            state.apply(c.events.take())
            XCTAssertEqual(state.transport, .paused)
            XCTAssertGreaterThan(state.positionClocks, 0)
            // a host-set tempo is taken without being echoed back
            let mark = world.process.log.count
            try await c.client.send(MidiEvent.cc14(.bpm, channel: 0, value: 1285))
            try await world.run(ms: 200)
            try await expectEqual(try await world.status().tempoX10, 1285)
            let echoed = world.midiEvents(from: mark).filter {
                if case let .controlChange(_, cc, _) = $0.event { return cc == Proto.CC.bpmMsb || cc == Proto.CC.bpmLsb }
                return false
            }
            XCTAssertTrue(echoed.isEmpty, "the host is not told about tempo changes it made itself")
        }
    }
}

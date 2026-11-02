// The real DeviceClient against the real firmware Engine: requests, replies, NAKs, persistence, restarts, streaming.
// The unit tests in EPDJ3000ProtocolTests use a fake device written from the spec; here the other end is the firmware.

import EPDJ3000Protocol
import XCTest

final class DeviceClientE2ETests: E2ETestCase {
    func testGetInfoDescribesTheUnitTheSimulatorBooted() async throws {
        try await withWorld("units=1 deck0=0") { world in
            let c = world.client(deck: 0)
            let info = try await c.client.getInfo()
            XCTAssertEqual(info.protocolVersion, Proto.protocolVersion)
            XCTAssertEqual(info.deck, Deck.a.rawValue)
            XCTAssertEqual(info.mode, Mode.dj.rawValue)
            XCTAssertEqual(info.platterSource, PlatterSource.touchRing.rawValue)
            XCTAssertEqual(Caps(rawValue: info.caps), [.touchRing, .trsMidi, .link, .statusLed])
            XCTAssertEqual(info.serial, 0xA0B0_C000)
            XCTAssertGreaterThanOrEqual(info.uptimeS, 3, "the unit has been up for the 3.6 s settle")
            XCTAssertLessThanOrEqual(info.uptimeS, 5)
            XCTAssertTrue(info.fwMajor | info.fwMinor | info.fwPatch != 0, "firmware reports a version")
        }
    }

    func testBroadcastAddressReachesTheUnitAndAnotherDeckIdDoesNot() async throws {
        try await withWorld("units=1 deck0=1") { world in
            let any = world.client(deck: Proto.SysEx.deckAny)
            let info = try await any.client.getInfo()
            XCTAssertEqual(info.deck, 1)

            // Addressed to Deck A, a Deck B unit stays silent: the client gives up after its (virtual) timeout.
            let wrong = world.client(deck: 0)
            async let attempt: Result<Info, Error> = {
                do { return .success(try await wrong.client.getInfo()) } catch { return .failure(error) }
            }()
            await world.waitForSleepers()  // the request is out and its timeout is armed
            try await world.run(ms: 1500)
            guard case let .failure(error) = await attempt else { return XCTFail("a Deck A request was answered by Deck B") }
            XCTAssertEqual(error as? DeviceError, .timeout(.getInfo))
            let right = world.client(deck: 1)
            let again = try await right.client.getInfo()
            XCTAssertEqual(again.deck, 1)
        }
    }

    func testDumpConfigReturnsEveryKeyTheContractDefinesAndEndsOnItsAck() async throws {
        try await withWorld("units=1 deck0=0") { world in
            let c = world.client()
            let dump = try await c.client.dumpConfig()
            XCTAssertEqual(Set(dump.keys), Set(ConfigKey.allCases), "one CONFIG frame per key, then ACK[GET_CONFIG] ends the dump")
            for desc in ConfigTable.all {
                XCTAssertEqual(dump[desc.key], desc.defaultValue, "factory default of \(desc.name)")
            }
            // and the single-key form agrees with the dump
            for key in [ConfigKey.faderRole, .touchOnPermille, .linkGroup, .buttonDebounceMs] {
                let single = try await c.client.getConfig(key)
                XCTAssertEqual(single, dump[key], "\(key)")
            }
            let sim = try await world.status()
            XCTAssertEqual(sim.cfg, ConfigKey.allCases.map { Int(dump[$0]!) }, "what the host read is what the firmware holds")
        }
    }

    func testFaderRoleDefaultIsPitchAsContractV12Says() async throws {
        try await withWorld { world in
            let role = try await world.client().client.getConfig(.faderRole)
            XCTAssertEqual(role, Int32(FaderRole.pitch.rawValue))
        }
    }

    func testSetGetSaveAndLoadDefaultsSemantics() async throws {
        try await withWorld { world in
            let c = world.client().client
            let key = ConfigKey.tapMaxMs
            let factory = ConfigTable.desc(key).defaultValue

            try await c.setConfig(key, 222)
            let live = try await c.getConfig(key)
            XCTAssertEqual(live, 222)
            var sim = try await world.status()
            XCTAssertEqual(sim.cfg[Int(key.rawValue)], 222, "applies live")
            XCTAssertEqual(sim.stored[Int(key.rawValue)], Int(factory), "but is not persisted until SAVE_CONFIG")

            try await c.saveConfig()
            sim = try await world.status()
            XCTAssertEqual(sim.stored[Int(key.rawValue)], 222, "SAVE_CONFIG persisted it")
            XCTAssertEqual(sim.int("saves"), 1)

            try await c.loadDefaults()
            try await expectEqual(try await c.getConfig(key), factory, "LOAD_DEFAULTS resets the live config")
            sim = try await world.status()
            XCTAssertEqual(sim.stored[Int(key.rawValue)], 222, "...without touching what is stored")

            // Power cycle: the unit comes back with what was stored, not what was live.
            try await c.setConfig(.tapMoveTicks, 20)
            _ = try await world.command("reboot u=0")
            try await world.run(ms: 500)
            try await expectEqual(try await c.getConfig(key), 222, "the saved value survives a restart")
            try await expectEqual(try await c.getConfig(.tapMoveTicks), ConfigTable.desc(.tapMoveTicks).defaultValue, "the unsaved one does not")
        }
    }

    func testRejectedValuesAreNakedWithTheContractsErrorCodes() async throws {
        try await withWorld { world in
            let c = world.client().client
            let tapMax = ConfigTable.desc(.tapMaxMs)
            do {
                try await c.setConfig(.tapMaxMs, tapMax.minValue - 1)
                XCTFail("below the range")
            } catch { XCTAssertEqual(error as? DeviceError, .nak(.setConfig, .outOfRange)) }
            do {
                try await c.setConfig(.tapMaxMs, tapMax.maxValue + 1)
                XCTFail("above the range")
            } catch { XCTAssertEqual(error as? DeviceError, .nak(.setConfig, .outOfRange)) }
            do {
                try await c.setConfig(.deckId, 1)
                XCTFail("deckId is only changed through SET_DECK")
            } catch { XCTAssertEqual(error as? DeviceError, .nak(.setConfig, .notSupported)) }
            try await expectEqual(try await c.getConfig(.tapMaxMs), tapMax.defaultValue, "a rejected SET_CONFIG changes nothing")

            _ = try await world.command("platform u=0 saveok=0")
            do {
                try await c.saveConfig()
                XCTFail("flash failure")
            } catch { XCTAssertEqual(error as? DeviceError, .nak(.saveConfig, .flashError)) }
            do {
                try await c.testTone(on: true, freqHz: 440, attenDb: 12)
                XCTFail("no DAC on this unit")
            } catch { XCTAssertEqual(error as? DeviceError, .nak(.testTone, .notSupported)) }
        }
    }

    func testEveryConfigKeyAcceptsItsContractRangeEndsAndRejectsOutsideThem() async throws {
        try await withWorld { world in
            let c = world.client().client
            for desc in ConfigTable.all where desc.key != .deckId {
                for value in [desc.minValue, desc.maxValue] {
                    try await c.setConfig(desc.key, value)
                    try await expectEqual(try await c.getConfig(desc.key), value, "\(desc.name)=\(value)")
                }
                if desc.maxValue < Int32.max {
                    do {
                        try await c.setConfig(desc.key, desc.maxValue + 1)
                        XCTFail("\(desc.name)=\(desc.maxValue + 1) must be out of range")
                    } catch { XCTAssertEqual(error as? DeviceError, .nak(.setConfig, .outOfRange), desc.name) }
                }
                if desc.minValue > Int32.min {
                    do {
                        try await c.setConfig(desc.key, desc.minValue - 1)
                        XCTFail("\(desc.name)=\(desc.minValue - 1) must be out of range")
                    } catch { XCTAssertEqual(error as? DeviceError, .nak(.setConfig, .outOfRange), desc.name) }
                }
            }
        }
    }

    func testSetDeckAcksWithTheOldDeckRestartsTheUnitAndTheClientFollowsTheNewId() async throws {
        try await withWorld("units=1 deck0=0") { world in
            let c = world.client(deck: 0)
            try await expectEqual(try await c.client.getInfo().deck, 0)
            try await c.client.setDeck(.b)
            let afterAck = try await world.status()
            XCTAssertEqual(afterAck.stored[Int(ConfigKey.deckId.rawValue)], 1, "the new deck is persisted")
            XCTAssertEqual(afterAck.cfg[Int(ConfigKey.midiChannel.rawValue)], 16, "channel stays 'auto'")

            try await world.run(ms: 1000)  // flush, restart, USB re-enumeration
            let info = try await c.client.getInfo()  // the client now addresses deck 1
            XCTAssertEqual(info.deck, 1, "the unit came back as Deck B")
            let target = await c.client.target
            XCTAssertEqual(target, 1)
            try await expectEqual(try await c.client.getConfig(.deckId), 1)

            // LOAD_DEFAULTS keeps the deck (contract v1.1)
            try await c.client.setConfig(.tapMaxMs, 200)
            try await c.client.loadDefaults()
            try await expectEqual(try await c.client.getConfig(.deckId), 1)
            try await expectEqual(try await c.client.getConfig(.tapMaxMs), ConfigTable.desc(.tapMaxMs).defaultValue)
        }
    }

    func testIdentifyRebootAndTestToneAreAcknowledged() async throws {
        try await withWorld("units=1 dac=1") { world in
            let c = world.client().client
            try await c.identify(seconds: 2)
            var lit = Set<Bool>()
            for _ in 0..<20 {  // 2 Hz blink: on and off both show up within a second
                try await world.run(ms: 50)
                lit.insert(try await world.status().led.bool("identify"))
            }
            XCTAssertEqual(lit, [true, false], "IDENTIFY blinks the status LED")
            try await c.testTone(on: true, freqHz: 1000, attenDb: 6)
            try await c.testTone(on: false)
            do {
                try await c.testTone(on: true, freqHz: 10, attenDb: 6)
                XCTFail("10 Hz is below the contract's 20 Hz")
            } catch { XCTAssertEqual(error as? DeviceError, .nak(.testTone, .outOfRange)) }
            try await c.reboot(.normal)
            try await world.run(ms: 500)
            try await expectEqual(try await world.status().int("reboots"), 1)
            let info = try await c.getInfo()
            XCTAssertLessThan(info.uptimeS, 2, "the unit restarted")
        }
    }
}

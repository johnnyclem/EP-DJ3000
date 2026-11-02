import XCTest
@testable import EPDJ3000Protocol

private struct Boom: Error {}

final class DeviceClientTests: XCTestCase {
    private let vectors = VectorFile.shared

    /// Happy-path tests use the system clock with a short timeout, so a lost reply fails fast instead of hanging.
    private func make(_ dev: FakeDevice, deck: UInt8 = Proto.SysEx.deckAny, clock: DeviceClock = SystemClock(),
                      timeout: Double = 2) -> DeviceClient {
        DeviceClient(transport: dev, deck: deck, clock: clock, timeout: timeout)
    }

    /// Runs `body` as a request, waits until the device has seen it and the timeout is armed.
    private func inFlight<T: Sendable>(_ dev: FakeDevice, _ clock: ManualClock, requests: Int = 1,
                                       _ body: @escaping @Sendable () async throws -> T) async -> Task<T, Error> {
        let t = Task { try await body() }
        await waitUntil("request sent") { dev.read { $0.requests.count } == requests && clock.sleeperCount == 1 }
        return t
    }

    // MARK: wire format

    func testRequestsMatchTheSharedFrameVectors() async throws {
        let a = FakeDevice(deck: .a)
        let clientA = make(a, deck: 0)
        _ = try await clientA.getInfo()
        XCTAssertEqual(a.read { $0.requestBytes.last }, vectors.frame("getInfo deck A").frame)
        _ = try await clientA.dumpConfig()
        XCTAssertEqual(a.read { $0.requestBytes.last }, vectors.frame("getConfig all").frame)

        let b = FakeDevice(deck: .b)
        try await make(b, deck: 1).setConfig(.platterSource, 1)
        XCTAssertEqual(b.read { $0.requestBytes.last }, vectors.frame("setConfig platterSource=1 deck B").frame)

        let any = FakeDevice(deck: .a)
        try await make(any).setConfig(.faderRawMax, 4055)
        XCTAssertEqual(any.read { $0.requestBytes.last }, vectors.frame("setConfig faderRawMax=4055 any deck").frame)
        XCTAssertEqual(any.read { $0.config[Int(ConfigKey.faderRawMax.rawValue)] }, 4055)
    }

    func testNegativeValueIsEncodedAsTwosComplementAndTheNakIsMapped() async {
        let dev = FakeDevice(deck: .a)
        let client = make(dev, deck: 0)
        await XCTAssertThrowsDeviceError(.nak(.setConfig, .outOfRange)) { try await client.setConfig(.bpmX10, -5) }
        XCTAssertEqual(dev.read { $0.requestBytes.last }, vectors.frame("setConfig negative value").frame)
    }

    func testCommandPayloads() async throws {
        let dev = FakeDevice()
        let client = make(dev)
        try await client.testTone(on: true, freqHz: 1000, attenDb: 6)
        XCTAssertEqual(dev.read { $0.lastTone }, [1, 0xE8, 0x03, 6])
        try await client.testTone(on: false)
        XCTAssertEqual(dev.read { $0.lastTone }, [0, 0xB8, 0x01, 12], "defaults: 440 Hz, 12 dB")
        try await client.identify(seconds: 7)
        XCTAssertEqual(dev.read { $0.lastIdentify }, 7)
        try await client.reboot(.bootloader)
        XCTAssertEqual(dev.read { $0.lastReboot }, 1)
        try await client.reboot()
        XCTAssertEqual(dev.read { $0.lastReboot }, 0)
    }

    // MARK: info and config

    func testGetInfo() async throws {
        let dev = FakeDevice(deck: .b)
        let info = try await make(dev).getInfo()
        XCTAssertEqual(info.deck, 1)
        XCTAssertEqual(info.serial, 0xDEADBEEF)
        XCTAssertEqual(info.fwMinor, 1)
        XCTAssertEqual(Caps(rawValue: info.caps), [.touchRing, .dac, .trsMidi, .link, .usbAudio])
    }

    func testUnknownProtocolVersionIsRefusedWithTheInfoAttached() async {
        let dev = FakeDevice()
        dev.mutate { $0.info.protocolVersion = 2 }
        let client = make(dev)
        do {
            _ = try await client.getInfo()
            XCTFail("expected a protocol mismatch")
        } catch let DeviceError.protocolMismatch(info) {
            XCTAssertEqual(info.protocolVersion, 2)
            XCTAssertEqual(info.fwPatch, 2, "the caller can still show which firmware this is")
        } catch {
            XCTFail("\(error)")
        }
    }

    func testDumpConfigReturnsEveryKeyAtItsDefault() async throws {
        let all = try await make(FakeDevice()).dumpConfig()
        XCTAssertEqual(all.count, ConfigKey.allCases.count)
        for d in ConfigTable.all { XCTAssertEqual(all[d.key], d.defaultValue, d.name) }
    }

    func testSetGetApplySaveAndLoadDefaults() async throws {
        let dev = FakeDevice()
        let client = make(dev)
        try await client.setConfig(.touchOnPermille, 33)
        var v = try await client.getConfig(.touchOnPermille)
        XCTAssertEqual(v, 33)

        try await client.apply([ConfigUpdate(key: .jogInvert, value: 1), ConfigUpdate(key: .touchOffPermille, value: 5)])
        let all = try await client.dumpConfig()
        XCTAssertEqual(all[.jogInvert], 1)
        XCTAssertEqual(all[.touchOffPermille], 5)
        XCTAssertEqual(all[.touchOnPermille], 33)

        XCTAssertEqual(dev.read { $0.saveCount }, 0, "SET_CONFIG alone is not persisted")
        try await client.saveConfig()
        XCTAssertEqual(dev.read { $0.saveCount }, 1)

        try await client.loadDefaults()
        v = try await client.getConfig(.touchOnPermille)
        XCTAssertEqual(v, 12)
    }

    func testApplyStopsAtTheFirstRejectedUpdate() async {
        let dev = FakeDevice()
        let client = make(dev)
        await XCTAssertThrowsDeviceError(.nak(.setConfig, .outOfRange)) {
            try await client.apply([ConfigUpdate(key: .jogInvert, value: 1),
                                    ConfigUpdate(key: .tapMaxMs, value: 9999),
                                    ConfigUpdate(key: .tapPlay, value: 0)])
        }
        XCTAssertEqual(dev.read { $0.config[Int(ConfigKey.jogInvert.rawValue)] }, 1)
        XCTAssertEqual(dev.read { $0.config[Int(ConfigKey.tapPlay.rawValue)] }, 1, "never reached")
    }

    // MARK: NAK mapping

    func testNakCodesMapToTypedErrors() async {
        let dev = FakeDevice()
        let client = make(dev)

        dev.mutate { $0.keyCount = 20 }  // an older firmware that has not heard of the newest keys
        await XCTAssertThrowsDeviceError(.nak(.getConfig, .badKey)) { try await client.getConfig(.buttonDebounceMs) }
        await XCTAssertThrowsDeviceError(.nak(.setConfig, .badKey)) { try await client.setConfig(.ledBrightness, 10) }

        dev.mutate { $0.hasDac = false }
        await XCTAssertThrowsDeviceError(.nak(.testTone, .notSupported)) { try await client.testTone(on: true) }

        dev.mutate { $0.failFlash = true }
        await XCTAssertThrowsDeviceError(.nak(.saveConfig, .flashError)) { try await client.saveConfig() }

        dev.mutate { $0.forcedNak = (command: SysExCmd.identify.rawValue, code: SysExError.busy.rawValue) }
        await XCTAssertThrowsDeviceError(.nak(.identify, .busy)) { try await client.identify() }

        dev.mutate { $0.forcedNak = (command: SysExCmd.identify.rawValue, code: 99) }
        await XCTAssertThrowsDeviceError(.unknownNak(.identify, code: 99)) { try await client.identify() }
    }

    func testANakForAnotherCommandDoesNotFailTheRequest() async throws {
        let dev = FakeDevice()
        dev.mutate { $0.silent = true }
        let client = make(dev)
        let t = Task { try await client.setConfig(.mode, 1) }
        await waitUntil("sent") { dev.read { $0.requests.count } == 1 }
        dev.inject(.nak(deck: 0, command: SysExCmd.saveConfig.rawValue, error: .flashError))
        dev.inject(.ack(deck: 0, command: SysExCmd.saveConfig.rawValue))
        dev.inject(.ack(deck: 0, command: SysExCmd.setConfig.rawValue))
        try await t.value
    }

    func testUnrelatedAndStaleRepliesAreIgnored() async throws {
        let dev = FakeDevice()
        dev.mutate { $0.silent = true }
        let client = make(dev)
        let t = Task { try await client.getConfig(.tapMaxMs) }
        await waitUntil("sent") { dev.read { $0.requests.count } == 1 }
        func config(_ key: ConfigKey, _ value: UInt8) -> SysExFrame {
            SysExFrame(deck: 0, reply: .config, payload: [key.rawValue, value, 0, 0, 0])
        }
        dev.inject(SysExFrame(deck: 0, reply: .info, payload: Info().serialized()))  // a reply to something else
        dev.inject(config(.mode, 9))                                                 // late answer to an earlier request
        dev.inject(config(.tapMaxMs, 77))
        let v = try await t.value
        XCTAssertEqual(v, 77)
    }

    func testMalformedReplyPayloadsAreReportedNotCrashed() async {
        let dev = FakeDevice()
        dev.mutate { $0.silent = true }
        let client = make(dev)

        let info = Task { try await client.getInfo() }
        await waitUntil("info sent") { dev.read { $0.requests.count } == 1 }
        dev.inject(SysExFrame(deck: 0, reply: .info, payload: [1, 2, 3]))
        await XCTAssertThrowsDeviceError(.malformedReply(.getInfo)) { try await info.value }

        let link = Task { try await client.linkStatus() }
        await waitUntil("link sent") { dev.read { $0.requests.count } == 2 }
        dev.inject(SysExFrame(deck: 0, reply: .link, payload: [3] + Peer().serialized()))  // claims 3 peers, carries 1
        await XCTAssertThrowsDeviceError(.malformedReply(.linkStatus)) { try await link.value }

        let cal = Task { try await client.calAutoEnd(.fader, apply: true) }
        await waitUntil("cal sent") { dev.read { $0.requests.count } == 3 }
        dev.inject(SysExFrame(deck: 0, reply: .calResult, payload: [0, 1]))
        await XCTAssertThrowsDeviceError(.malformedReply(.calEnd)) { try await cal.value }
    }

    // MARK: link

    func testLinkStatusParsesPeers() async throws {
        let dev = FakeDevice()
        let client = make(dev)
        var peers = try await client.linkStatus()
        XCTAssertEqual(peers, [])

        let vec = vectors.structs.first { $0.struct == "Peer" }!
        let p1 = Peer(bytes: vec.bytes)!
        let p2 = Peer(deck: 0, flags: PeerFlags([.usbHost, .isMaster]).rawValue, rssi: -41, rttUs: 900, offsetUs: 12)
        dev.mutate { $0.peers = [p1, p2] }
        peers = try await client.linkStatus()
        XCTAssertEqual(peers, [p1, p2])
        XCTAssertEqual(peers[0].rssi, -60)
        XCTAssertEqual(PeerFlags(rawValue: peers[1].flags), [.usbHost, .isMaster])
    }

    // MARK: calibration on the device

    func testDeviceAssistedSweepAppliesTheInsetRange() async throws {
        let dev = FakeDevice()
        let client = make(dev)
        try await client.calAutoBegin(.fader)
        for raw in [1000, 100, 2500, 3900, 2000] { dev.moveControl(.fader, raw: raw) }
        let r = try await client.calAutoEnd(.fader, apply: true)
        XCTAssertEqual(r.kind, CalKind.fader.rawValue)
        XCTAssertEqual(r.applied, 1)
        XCTAssertEqual(r.rawMin, 138)   // 100 + 1% of 3800
        XCTAssertEqual(r.rawMax, 3862)
        let min = try await client.getConfig(.faderRawMin)
        let max = try await client.getConfig(.faderRawMax)
        XCTAssertEqual(min, 138)
        XCTAssertEqual(max, 3862)
    }

    func testDeviceAssistedSweepWithoutApplyLeavesConfigAlone() async throws {
        let dev = FakeDevice()
        let client = make(dev)
        try await client.calAutoBegin(.knob)
        dev.moveControl(.knob, raw: 0)
        dev.moveControl(.knob, raw: 4095)
        let r = try await client.calAutoEnd(.knob, apply: false)
        XCTAssertEqual(r.applied, 0)
        let v = try await client.getConfig(.knobRawMax)
        XCTAssertEqual(v, 4095, "default, untouched")
        XCTAssertEqual(dev.read { $0.config[Int(ConfigKey.knobRawMin.rawValue)] }, 0)
    }

    func testDeviceAssistedSweepFailures() async {
        let dev = FakeDevice()
        let client = make(dev)
        await XCTAssertThrowsDeviceError(.nak(.calEnd, .calNotActive)) { try await client.calAutoEnd(.fader, apply: true) }

        try? await client.calAutoBegin(.fader)
        dev.moveControl(.fader, raw: 1000)
        dev.moveControl(.fader, raw: 2000)
        await XCTAssertThrowsDeviceError(.nak(.calEnd, .calFailed)) { try await client.calAutoEnd(.fader, apply: true) }
        await XCTAssertThrowsDeviceError(.nak(.calEnd, .calNotActive)) { try await client.calAutoEnd(.fader, apply: true) }

        try? await client.calAutoBegin(.fader)
        await XCTAssertThrowsDeviceError(.nak(.calEnd, .calNotActive)) { try await client.calAutoEnd(.knob, apply: true) }
    }

    // MARK: deck addressing

    func testRequestsForAnotherDeckAreNotAnswered() async throws {
        let dev = FakeDevice(deck: .b)
        let clock = ManualClock()
        let client = make(dev, deck: 0, clock: clock, timeout: 1)
        let t = await inFlight(dev, clock) { try await client.getInfo() }
        clock.advance(by: 1)
        await XCTAssertThrowsDeviceError(.timeout(.getInfo)) { try await t.value }

        await client.setTarget(1)
        let info = try await client.getInfo()
        XCTAssertEqual(info.deck, 1)
        let target = await client.target
        XCTAssertEqual(target, 1)
    }

    func testRepliesFromAnotherDeckAreIgnored() async throws {
        let dev = FakeDevice(deck: .b)
        dev.mutate { $0.silent = true }
        let client = make(dev, deck: 1)
        let t = Task { try await client.getInfo() }
        await waitUntil("sent") { dev.read { $0.requests.count } == 1 }

        dev.inject(SysExFrame(deck: 0, reply: .info, payload: Info(protocolVersion: 1, serial: 111).serialized()))
        dev.inject(SysExFrame(deck: 1, reply: .info, payload: Info(protocolVersion: 1, deck: 1, serial: 222).serialized()))
        let info = try await t.value
        XCTAssertEqual(info.serial, 222)
    }

    func testAnyDeckTargetAcceptsWhoeverAnswers() async throws {
        for deck in Deck.allCases {
            let info = try await make(FakeDevice(deck: deck)).getInfo()
            XCTAssertEqual(info.deck, deck.rawValue)
        }
    }

    func testSetDeckReAddressesTheClientToTheNewId() async throws {
        let dev = FakeDevice(deck: .a)
        let client = make(dev, deck: 0)
        try await client.setDeck(.b)  // the ACK still carries deck 0
        var target = await client.target
        XCTAssertEqual(target, 1)
        XCTAssertEqual(dev.read { $0.deck }, 1)
        let info = try await client.getInfo()
        XCTAssertEqual(info.deck, 1)

        let anyDev = FakeDevice(deck: .a)
        let anyClient = make(anyDev)
        try await anyClient.setDeck(.b)
        target = await anyClient.target
        XCTAssertEqual(target, Proto.SysEx.deckAny, "an any-deck client stays any-deck")
    }

    func testSetDeckSucceedsWhicheverIdTheAckCarries() async throws {
        let dev = FakeDevice(deck: .a)
        dev.mutate { $0.setDeckAcksWithNewId = true }
        let client = make(dev, deck: 0)
        try await client.setDeck(.b)
        let target = await client.target
        XCTAssertEqual(target, 1)
    }

    func testAnAckFromAnotherDeckDoesNotCompleteAnOrdinaryRequest() async throws {
        let dev = FakeDevice(deck: .a)
        dev.mutate { $0.silent = true }
        let clock = ManualClock()
        let client = make(dev, deck: 0, clock: clock, timeout: 1)
        let t = await inFlight(dev, clock) { try await client.setConfig(.mode, 1) }
        dev.inject(.ack(deck: 1, command: SysExCmd.setConfig.rawValue))
        try await Task.sleep(nanoseconds: 30_000_000)  // let the receive loop see it
        clock.advance(by: 1)
        await XCTAssertThrowsDeviceError(.timeout(.setConfig)) { try await t.value }
    }

    // MARK: timeouts, serialisation, cancellation

    func testTimeoutThenRecovery() async throws {
        let dev = FakeDevice()
        dev.mutate { $0.silent = true }
        let clock = ManualClock()
        let client = make(dev, clock: clock, timeout: 1)

        let t = await inFlight(dev, clock) { try await client.getInfo() }
        clock.advance(by: 0.9)
        XCTAssertEqual(clock.sleeperCount, 1, "not yet")
        clock.advance(by: 0.1)
        await XCTAssertThrowsDeviceError(.timeout(.getInfo)) { try await t.value }

        dev.mutate { $0.silent = false }
        let info = try await client.getInfo()
        XCTAssertEqual(info.serial, 0xDEADBEEF)
        XCTAssertEqual(clock.sleeperCount, 0, "the finished request's timeout was cancelled")
    }

    func testEachCommandReportsItsOwnNameInTheTimeout() async {
        let dev = FakeDevice()
        dev.mutate { $0.silent = true }
        let clock = ManualClock()
        let client = make(dev, clock: clock, timeout: 1)
        let t = await inFlight(dev, clock) { try await client.setConfig(.mode, 1) }
        clock.advance(by: 1)
        await XCTAssertThrowsDeviceError(.timeout(.setConfig)) { try await t.value }
    }

    func testFlashCommandsGetTheLongerTimeout() async {
        let dev = FakeDevice()
        dev.mutate { $0.silent = true }
        let clock = ManualClock()
        let client = DeviceClient(transport: dev, clock: clock, timeout: 1, slowTimeout: 3)
        let t = await inFlight(dev, clock) { try await client.saveConfig() }
        clock.advance(by: 2.9)
        XCTAssertEqual(clock.sleeperCount, 1)
        clock.advance(by: 0.1)
        await XCTAssertThrowsDeviceError(.timeout(.saveConfig)) { try await t.value }
    }

    func testDumpFromAnOlderFirmwareWithFewerKeysTimesOut() async {
        let dev = FakeDevice()
        dev.mutate { $0.keyCount = 20 }
        let clock = ManualClock()
        let client = make(dev, clock: clock, timeout: 1)
        let t = Task { try await client.dumpConfig() }
        await waitUntil("20 frames in, still waiting") { dev.read { $0.requests.count } == 1 && clock.sleeperCount == 1 }
        clock.advance(by: 1)
        await XCTAssertThrowsDeviceError(.timeout(.getConfig)) { try await t.value }
    }

    func testRequestsRunOneAtATimeAndEachGetsItsOwnAnswer() async throws {
        let dev = FakeDevice()
        dev.mutate { $0.holdReplies = true }
        let client = make(dev, timeout: 5)

        let a = Task { try await client.getConfig(.mode) }
        await waitUntil("A sent") { dev.read { $0.requests.count } == 1 }
        let b = Task { try await client.getConfig(.tapMaxMs) }
        let c = Task { try await client.getConfig(.bpmX10) }
        try await Task.sleep(nanoseconds: 50_000_000)
        XCTAssertEqual(dev.read { $0.requests.count }, 1, "B and C must wait for A's reply")

        dev.mutate { $0.holdReplies = false }
        dev.releaseHeld()
        let results = try await [a.value, b.value, c.value]
        XCTAssertEqual(results, [0, 180, 1200])
        XCTAssertEqual(dev.read { $0.requests.count }, 3)
    }

    func testCancellingARequestFreesTheClient() async throws {
        let dev = FakeDevice()
        dev.mutate { $0.silent = true }
        let clock = ManualClock()
        let client = make(dev, clock: clock, timeout: 1)
        let t = await inFlight(dev, clock) { try await client.getInfo() }
        t.cancel()
        do {
            _ = try await t.value
            XCTFail("expected cancellation")
        } catch is CancellationError {
        } catch {
            XCTFail("\(error)")
        }
        XCTAssertEqual(clock.sleeperCount, 0, "no timeout left behind")

        dev.mutate { $0.silent = false }
        _ = try await client.getInfo()
    }

    func testSendFailureSurfacesAndTheClientRecovers() async throws {
        let dev = FakeDevice()
        let client = make(dev)
        dev.mutate { $0.sendError = Boom() }
        do {
            _ = try await client.getInfo()
            XCTFail("expected a send failure")
        } catch DeviceError.sendFailed {
        } catch {
            XCTFail("\(error)")
        }
        dev.mutate { $0.sendError = nil }
        _ = try await client.getInfo()
    }

    // MARK: lifecycle

    func testCloseFailsTheInFlightRequestAndEverythingAfterIt() async {
        let dev = FakeDevice()
        dev.mutate { $0.silent = true }
        let clock = ManualClock()
        let client = make(dev, clock: clock, timeout: 1)
        let events = client.events()
        let t = await inFlight(dev, clock) { try await client.getInfo() }

        await client.close()
        await XCTAssertThrowsDeviceError(.transportClosed) { try await t.value }
        await XCTAssertThrowsDeviceError(.transportClosed) { try await client.getInfo() }
        await XCTAssertThrowsDeviceError(.transportClosed) { try await client.send([.start]) }
        await client.close()  // idempotent
        let drained = await collect(events, count: 1)
        XCTAssertEqual(drained, [], "streams end on close")
    }

    func testTransportGoingAwayFailsPendingRequestsAndEndsStreams() async {
        let dev = FakeDevice()
        dev.mutate { $0.silent = true }
        let clock = ManualClock()
        let client = make(dev, clock: clock, timeout: 1)
        let telemetry = client.telemetry()
        let t = await inFlight(dev, clock) { try await client.getInfo() }

        dev.disconnect()  // unplugged
        await XCTAssertThrowsDeviceError(.transportClosed) { try await t.value }
        let drained = await collect(telemetry, count: 1)
        XCTAssertEqual(drained, [])
        await XCTAssertThrowsDeviceError(.transportClosed) { try await client.identify() }
    }

    func testDroppedClientIsNotKeptAliveByItsReceiveLoop() async throws {
        let dev = FakeDevice()
        weak var weakClient: DeviceClient?
        do {
            let client = make(dev)
            weakClient = client
            _ = try await client.getInfo()
        }
        await waitUntil("client deallocated") { weakClient == nil }
    }

    // MARK: receiving

    func testSysExSplitInto3ByteChunksWithRealtimeBytesInsideStillCompletes() async throws {
        let dev = FakeDevice()
        dev.mutate { $0.chunkSize = 3; $0.interleaveRealtime = true }
        let client = make(dev)
        let events = client.events()

        let info = try await client.getInfo()
        XCTAssertEqual(info.serial, 0xDEADBEEF)
        let all = try await client.dumpConfig()
        XCTAssertEqual(all.count, 32)

        let seen = await collect(events, count: 1)
        XCTAssertEqual(seen, [.clock], "the interleaved real-time bytes surface as events, in addition")
    }

    func testNonDeviceTrafficGoesToEventsAndDeviceFramesDoNot() async throws {
        let dev = FakeDevice()
        let client = make(dev)
        let events = client.events()
        let timecode = vectors.mtc[0].fullFrame

        dev.inject([0x90, 38, 127])
        dev.inject(timecode)
        _ = try await client.getInfo()  // device frame: must not appear in events
        dev.inject([0xB0, 22, 70])

        let seen = await collect(events, count: 3)
        XCTAssertEqual(seen, [.noteOn(channel: 0, note: 38, velocity: 127), .sysex(timecode), .controlChange(channel: 0, controller: 22, value: 70)])
    }

    func testEventsAreNotDroppedWhenTheConsumerLags() async {
        let dev = FakeDevice()
        let client = make(dev)
        let events = client.events()
        for _ in 0..<2000 { dev.inject([0xB0, Proto.CC.jog, 65]) }  // 4x any sane buffer, before anyone reads
        let got = await collect(events, count: 2000)
        XCTAssertEqual(got.count, 2000, "a dropped jog event would shift the platter position for good")
        var state = ControllerState(channel: 0)
        state.apply(got)
        XCTAssertEqual(state.jogTicks, 2000)
    }

    func testEventsReachEverySubscriber() async {
        let dev = FakeDevice()
        let client = make(dev)
        let a = client.events()
        let b = client.events()
        dev.inject([0xF8, 0xFA])
        let (ea, eb) = await (collect(a, count: 2), collect(b, count: 2))
        XCTAssertEqual(ea, [.clock, .start])
        XCTAssertEqual(eb, [.clock, .start])
    }

    func testCorruptFramesAreCountedAndDoNotSatisfyARequest() async throws {
        let dev = FakeDevice()
        dev.mutate { $0.silent = true }
        let client = make(dev)
        let t = Task { try await client.getInfo() }
        await waitUntil("sent") { dev.read { $0.requests.count } == 1 }

        let good = SysExFrame(deck: 0, reply: .info, payload: Info(protocolVersion: 1, serial: 5).serialized()).encode()
        var badChecksum = good
        badChecksum[badChecksum.count - 2] ^= 0x01
        var badVersion = good
        badVersion[4] = 9
        dev.inject(badChecksum)
        dev.inject(badVersion)
        await waitUntil("both rejected") { await client.rejectedFrameCount == 2 }

        dev.inject(good)
        let info = try await t.value
        XCTAssertEqual(info.serial, 5)
        let rejected = await client.rejectedFrameCount
        XCTAssertEqual(rejected, 2)
    }

    func testSendingMidiToTheUnit() async throws {
        let dev = FakeDevice()
        let client = make(dev)
        let out: [MidiEvent] = MidiEvent.cc14(.bpm, channel: 0, value: 1285)
            + [.controlChange(channel: 0, controller: Proto.CC.transport, value: 1), .noteOn(channel: 0, note: Proto.Note.play, velocity: 127)]
        try await client.send(out)
        XCTAssertEqual(dev.read { $0.midiIn }, out)
    }

    // MARK: streaming

    private func telemetry(seq: UInt8, jog: Int32 = 0) -> Telemetry {
        Telemetry(seq: seq, faderRaw: 2048, jogTotal: jog, touchRaw: [1, 2, 3, 4, 5, 6, 7, 8])
    }

    func testTelemetryReachesEverySubscriberInOrder() async throws {
        let dev = FakeDevice()
        let client = make(dev)
        let a = client.telemetry()
        let b = client.telemetry()
        try await client.startStream(rateHz: 50)
        XCTAssertEqual(dev.read { $0.streamRate }, 50)
        let streaming = await client.isStreaming
        XCTAssertTrue(streaming)

        for i in 1...3 { dev.emit(telemetry(seq: UInt8(i), jog: Int32(i * -1000))) }
        let (ta, tb) = await (collect(a, count: 3), collect(b, count: 3))
        XCTAssertEqual(ta.map(\.seq), [1, 2, 3])
        XCTAssertEqual(ta.map(\.jogTotal), [-1000, -2000, -3000])
        XCTAssertEqual(ta, tb)
        XCTAssertEqual(ta[0].touchRaw, [1, 2, 3, 4, 5, 6, 7, 8])
    }

    func testTelemetryFromAnotherDeckIsIgnored() async throws {
        let dev = FakeDevice(deck: .a)
        let client = make(dev, deck: 0)
        let stream = client.telemetry()
        try await client.startStream(rateHz: 50)
        dev.inject(SysExFrame(deck: 1, reply: .telemetry, payload: telemetry(seq: 99).serialized()))
        dev.emit(telemetry(seq: 1))
        let got = await collect(stream, count: 1)
        XCTAssertEqual(got.map(\.seq), [1])
    }

    func testMalformedTelemetryIsCountedNotDelivered() async throws {
        let dev = FakeDevice()
        let client = make(dev)
        let stream = client.telemetry()
        dev.inject(SysExFrame(deck: 0, reply: .telemetry, payload: [1, 2, 3]))
        dev.inject(SysExFrame(deck: 0, reply: .telemetry, payload: telemetry(seq: 7).serialized()))
        let got = await collect(stream, count: 1)
        XCTAssertEqual(got.map(\.seq), [7])
        let rejected = await client.rejectedFrameCount
        XCTAssertEqual(rejected, 1)
    }

    func testStreamRateIsClampedToWhatTheDeviceAccepts() async throws {
        let dev = FakeDevice()
        let client = make(dev)
        try await client.startStream(rateHz: 500)
        XCTAssertEqual(dev.read { $0.streamRate }, 100)
        try await client.startStream(rateHz: 0)
        XCTAssertEqual(dev.read { $0.streamRate }, 1, "0 would mean off; use stopStream")
    }

    func testStreamIsRefreshedBeforeTheDeviceGivesUpAndStopsOnRequest() async throws {
        let dev = FakeDevice()
        let clock = ManualClock()
        let client = DeviceClient(transport: dev, clock: clock, timeout: 1, streamRefreshInterval: 2)
        try await client.startStream(rateHz: 50)
        XCTAssertEqual(dev.read { $0.streamRefreshes }, 1)

        for n in 2...3 {
            await waitUntil("keep-alive sleeping") { clock.sleeperCount == 1 }
            clock.advance(by: 2)
            await waitUntil("refresh #\(n)") { dev.read { $0.streamRefreshes } == n }
        }
        XCTAssertEqual(dev.read { $0.requests.last?.payload }, [50], "refresh repeats the same rate")

        try await client.stopStream()
        XCTAssertEqual(dev.read { $0.streamRate }, 0)
        let streaming = await client.isStreaming
        XCTAssertFalse(streaming)
        await waitUntil("keep-alive gone") { clock.sleeperCount == 0 }
        clock.advance(by: 60)
        try await Task.sleep(nanoseconds: 30_000_000)
        XCTAssertEqual(dev.read { $0.streamRefreshes }, 3, "nothing refreshes a stopped stream")
    }

    func testStreamGivesUpAfterThreeFailedRefreshes() async throws {
        let dev = FakeDevice()
        let clock = ManualClock()
        let client = DeviceClient(transport: dev, clock: clock, timeout: 1, streamRefreshInterval: 2)
        try await client.startStream(rateHz: 50)
        dev.mutate { $0.silent = true }

        for round in 1...3 {
            await waitUntil("keep-alive sleeping, round \(round)") { clock.sleeperCount == 1 }
            let before = dev.read { $0.requests.count }
            clock.advance(by: 2)
            await waitUntil("refresh sent, round \(round)") { dev.read { $0.requests.count } == before + 1 && clock.sleeperCount == 1 }
            clock.advance(by: 1)
        }
        await waitUntil("stream marked lost") { await !client.isStreaming }
        await waitUntil("keep-alive gone") { clock.sleeperCount == 0 }
    }

    func testStoppingAStreamThatNeverStartedIsHarmless() async throws {
        let dev = FakeDevice()
        try await make(dev).stopStream()
        XCTAssertEqual(dev.read { $0.requests.last?.payload }, [0])
    }
}

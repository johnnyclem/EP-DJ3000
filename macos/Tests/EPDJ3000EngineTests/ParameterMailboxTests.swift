import Foundation
import XCTest
@testable import EPDJ3000Engine
import EPDJ3000Protocol

final class ParameterMailboxTests: XCTestCase {
    // XCTest also instantiates the case just to discover tests, and never tears those instances down.
    private var slots: UnsafeMutablePointer<PlayerCommand>!

    override func setUp() {
        slots = .allocate(capacity: ParameterMailbox.commandCapacity)
        slots.initialize(repeating: .stop, count: ParameterMailbox.commandCapacity)
    }

    override func tearDown() {
        slots.deallocate()
        slots = nil
    }

    private func fetch(_ mailbox: ParameterMailbox, _ p: inout PlayerParameters, _ t: inout AudioBuffer?) -> ParameterMailbox.Fetch? {
        mailbox.fetch(parameters: &p, track: &t, commands: slots)
    }

    func testCommandsArriveInOrder() {
        let m = ParameterMailbox()
        var p = PlayerParameters()
        var t: AudioBuffer?
        m.send(.cueDown)
        m.send(.play)
        m.send(.cueUp)
        m.send(.seek(seconds: 2))
        let f = fetch(m, &p, &t)
        XCTAssertEqual(f?.commandCount, 4)
        XCTAssertEqual((0..<4).map { slots[$0] }, [.cueDown, .play, .cueUp, .seek(seconds: 2)])
        XCTAssertEqual(fetch(m, &p, &t)?.commandCount, 0, "each command is delivered once")
    }

    func testFullQueueDropsTheOldestNotTheNewest() {
        let m = ParameterMailbox()
        var p = PlayerParameters()
        var t: AudioBuffer?
        for i in 0..<(ParameterMailbox.commandCapacity + 6) { m.send(.seek(seconds: Double(i))) }
        let f = fetch(m, &p, &t)
        XCTAssertEqual(f?.commandCount, ParameterMailbox.commandCapacity)
        XCTAssertEqual(slots[0], .seek(seconds: 6))
        XCTAssertEqual(slots[ParameterMailbox.commandCapacity - 1], .seek(seconds: Double(ParameterMailbox.commandCapacity + 5)))
    }

    func testContendedFetchReturnsNilAndKeepsTheLastSnapshot() {
        let m = ParameterMailbox()
        var p = PlayerParameters()
        var t: AudioBuffer?
        m.update { $0.tempoRatio = 1.25 }
        XCTAssertNotNil(fetch(m, &p, &t))
        XCTAssertEqual(p.tempoRatio, 1.25)

        m.update { $0.tempoRatio = 0.5 }
        m.send(.play)
        m.withLockHeldForTesting {
            XCTAssertNil(fetch(m, &p, &t), "the render side must not wait")
            XCTAssertEqual(p.tempoRatio, 1.25, "and keeps what it had")
            m.publish(PlayerStatus())  // also non-blocking
        }
        let f = fetch(m, &p, &t)
        XCTAssertEqual(p.tempoRatio, 0.5, "news arrives on the next uncontended block")
        XCTAssertEqual(f?.commandCount, 1, "and the command was not lost")
    }

    func testStatusRoundTripAndSkippedWhenContended() {
        let m = ParameterMailbox()
        var s = PlayerStatus()
        s.playheadFrames = 123
        m.publish(s)
        XCTAssertEqual(m.status().playheadFrames, 123)
        var s2 = s
        s2.playheadFrames = 456
        m.withLockHeldForTesting { m.publish(s2) }
        XCTAssertEqual(m.status().playheadFrames, 123, "a contended publish is skipped, not queued")
    }

    func testTrackHandOffNeverFreesOnTheRenderSide() {
        let m = ParameterMailbox()
        var p = PlayerParameters()
        var render: AudioBuffer?
        var a: AudioBuffer? = Sine.buffer(frequency: 100, seconds: 0.1)
        weak var weakA = a

        m.load(a)
        XCTAssertTrue(fetch(m, &p, &render)!.trackChanged)
        XCTAssertTrue(render === a)
        a = nil
        XCTAssertNotNil(weakA, "the render side holds it")

        let b = Sine.buffer(frequency: 200, seconds: 0.1)
        m.load(b)
        XCTAssertTrue(fetch(m, &p, &render)!.trackChanged)
        XCTAssertTrue(render === b)
        XCTAssertNotNil(weakA, "swapped out on the render thread, parked in the mailbox, not freed there")

        m.load(Sine.buffer(frequency: 300, seconds: 0.1))  // the control side reaps the parked one
        XCTAssertNil(weakA)
        XCTAssertTrue(render === b, "and the render side is undisturbed until it fetches")
    }

    func testTrackPostedTwiceBeforeAFetchOnlyShowsTheLast() {
        let m = ParameterMailbox()
        var p = PlayerParameters()
        var render: AudioBuffer?
        var first: AudioBuffer? = Sine.buffer(frequency: 100, seconds: 0.1)
        weak var weakFirst = first
        m.load(first)
        first = nil
        let second = Sine.buffer(frequency: 200, seconds: 0.1)
        m.load(second)
        XCTAssertNil(weakFirst, "never seen by the render side, freed at once")
        _ = fetch(m, &p, &render)
        XCTAssertTrue(render === second)
    }

    func testUnloadingDeliversNil() {
        let m = ParameterMailbox()
        var p = PlayerParameters()
        var render: AudioBuffer? = Sine.buffer(frequency: 100, seconds: 0.1)
        m.load(nil)
        XCTAssertTrue(fetch(m, &p, &render)!.trackChanged)
        XCTAssertNil(render)
    }

    // MARK: through the player

    func testPlayerKeepsRunningOnItsLastSnapshotWhileTheControlSideHoldsTheLock() {
        let rig = Rig(track: Sine.buffer(frequency: 440, seconds: 4))
        rig.playFrom(1.0)
        XCTAssertEqual(rig.status.speed, 1)

        rig.core.update { $0.tempoRatio = 1.5 }
        rig.core.send(.pause)
        var heldOut = [Float]()
        rig.core.mailbox.withLockHeldForTesting {
            var l = [Float](repeating: 0, count: 480), r = l
            rig.core.render(left: &l, right: &r, frameCount: 480)  // must return, not deadlock
            heldOut = l
        }
        XCTAssertTrue(allFinite(heldOut))
        XCTAssertGreaterThan(heldOut.map { abs($0) }.max()!, 0.5, "still playing at the old settings")

        rig.render(seconds: 0.3)
        XCTAssertEqual(rig.status.transport, .paused, "the pause was not lost")
    }

    func testControlThreadHammeringWhileRendering() {
        let tracks = [Sine.buffer(frequency: 300, seconds: 1), Sine.buffer(frequency: 500, seconds: 0.5),
                      Sine.buffer(frequency: 700, seconds: 2)]
        let core = ScratchPlayerCore(sampleRate: 48_000)
        core.load(tracks[0])
        let done = DispatchSemaphore(value: 0)
        let stop = ManagedFlag()

        DispatchQueue.global().async {
            var rng = XorShift(state: 12345)
            var i = 0
            while !stop.isSet && i < 200_000 {
                i += 1
                switch rng.int(8) {
                case 0: core.load(tracks[rng.int(3)])
                case 1: core.send(.togglePlay)
                case 2: core.send(rng.int(2) == 0 ? .cueDown : .cueUp)
                case 3: core.setTouched(rng.int(2) == 0)
                case 4: core.setJog(ticksPerSecond: rng.range(-4000, 4000))
                case 5: core.update { $0.faderPosition = Float(rng.unit()) }
                case 6: core.send(.seek(seconds: rng.range(0, 1)))
                default: _ = core.status
                }
            }
            done.signal()
        }

        var l = [Float](repeating: 0, count: 64), r = l
        var worst: Float = 0
        for _ in 0..<5000 {
            core.render(left: &l, right: &r, frameCount: 64)
            XCTAssertTrue(allFinite(l) && allFinite(r))
            worst = max(worst, l.map { abs($0) }.max()!)
        }
        stop.set()
        done.wait()
        XCTAssertLessThanOrEqual(worst, 1.0)
    }
}

final class ManagedFlag: @unchecked Sendable {
    private let lock = NSLock()
    private var value = false
    var isSet: Bool { lock.lock(); defer { lock.unlock() }; return value }
    func set() { lock.lock(); value = true; lock.unlock() }
}

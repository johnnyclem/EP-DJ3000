// Control thread -> render thread hand-off for ScratchPlayerCore.
//
// One pthread mutex guards everything. The control side takes it with a blocking lock (it holds it for a few
// stores). The render side only ever *tries*: if the control side happens to hold the lock, the render thread
// keeps its last snapshot and picks up the news on the next block, so it never waits. Three kinds of traffic:
//   * parameters: a value snapshot, latest wins;
//   * commands: a short FIFO, because a press and a release must both arrive, in order;
//   * the loaded track, handed over by reference, and the render thread's status coming back.

import EPDJ3000Protocol
import Foundation

public struct PlayerParameters: Equatable, Sendable {
    /// Platter touched: speed follows the jog velocity instead of the transport.
    public var touched = false
    /// REV held: play backwards (unless the platter is touched).
    public var reverseHeld = false
    /// Latest estimated platter velocity, signed, positive = clockwise = forwards.
    public var jogTicksPerSecond: Float = 0
    /// Bumped whenever `jogTicksPerSecond` is refreshed. A velocity that stops being refreshed fades to zero.
    public var jogSerial: UInt32 = 0
    /// Nominal speed while playing (pitch fader): 1.0 = original tempo.
    public var tempoRatio: Float = 1
    /// Fader position, 0 = closed, 1 = open.
    public var faderPosition: Float = 1
    public var faderCurve: FaderCurve = .sharpCut
    /// Knob trim, 0...1.
    public var volume: Float = 1
    /// After the platter is released, speed settles (to within 1 %) onto the nominal speed in this many seconds.
    public var releaseEaseSeconds: Float = 0.120

    public init() {}

    /// The render thread must never trap or poison its state on a bad value.
    mutating func sanitize() {
        if !jogTicksPerSecond.isFinite { jogTicksPerSecond = 0 }
        tempoRatio = tempoRatio.isFinite ? min(4, max(0.1, tempoRatio)) : 1
        faderPosition = faderPosition.isFinite ? min(1, max(0, faderPosition)) : 1
        volume = volume.isFinite ? min(1, max(0, volume)) : 1
        releaseEaseSeconds = releaseEaseSeconds.isFinite ? min(5, max(0.005, releaseEaseSeconds)) : 0.120
    }
}

public enum PlayerCommand: Equatable, Sendable {
    case play
    case pause
    /// Halt and return to the cue point.
    case stop
    case togglePlay
    /// CDJ-style CUE button: playing -> back to cue and pause; paused -> set cue here and preview while held.
    case cueDown
    case cueUp
    case setCueHere
    case seek(seconds: Double)
}

/// What the render thread last reported. Refreshed once per block; stale by at most one block.
public struct PlayerStatus: Equatable, Sendable {
    public var transport: TransportState = .stopped
    public var playheadFrames = 0.0
    public var trackFrames = 0
    public var trackSampleRate = 0.0
    public var cueFrames = 0.0
    /// Signed playback speed, 1.0 = original tempo forwards.
    public var speed = 0.0
    /// CUE is held on a paused deck and the track is playing from the cue point.
    public var previewing = false
    public var touched = false
    /// How many times a track has played out to its end.
    public var endCount: UInt32 = 0
    /// Smoothed output gain (fader x knob).
    public var gain: Float = 1

    public init() {}

    public var hasTrack: Bool { trackFrames > 0 }
    public var playheadSeconds: Double { trackSampleRate > 0 ? playheadFrames / trackSampleRate : 0 }
    public var cueSeconds: Double { trackSampleRate > 0 ? cueFrames / trackSampleRate : 0 }
    public var durationSeconds: Double { trackSampleRate > 0 ? Double(trackFrames) / trackSampleRate : 0 }
    /// Not playing and parked on the cue point (within 1 ms).
    public var isAtCue: Bool {
        hasTrack && transport != .playing && abs(playheadFrames - cueFrames) <= trackSampleRate * 0.001
    }
    /// What the CUE LED shows: parked on the cue point, or previewing from it.
    public var cueLit: Bool { previewing || isAtCue }
}

final class ParameterMailbox: @unchecked Sendable {
    static let commandCapacity = 64

    struct Fetch {
        var commandCount = 0
        var trackChanged = false
    }

    private let mutex: UnsafeMutablePointer<pthread_mutex_t>
    private var parameters = PlayerParameters()
    private let queue: UnsafeMutablePointer<PlayerCommand>
    private var queueHead = 0
    private var queueCount = 0
    private var pendingTrack: AudioBuffer?
    private var retiredTrack: AudioBuffer?
    private var trackDirty = false
    private var published = PlayerStatus()

    init() {
        mutex = .allocate(capacity: 1)
        pthread_mutex_init(mutex, nil)
        queue = .allocate(capacity: Self.commandCapacity)
        queue.initialize(repeating: .stop, count: Self.commandCapacity)
    }

    deinit {
        pthread_mutex_destroy(mutex)
        mutex.deallocate()
        queue.deallocate()
    }

    // MARK: control thread

    func update(_ body: (inout PlayerParameters) -> Void) {
        pthread_mutex_lock(mutex)
        defer { pthread_mutex_unlock(mutex) }
        body(&parameters)
    }

    func currentParameters() -> PlayerParameters {
        pthread_mutex_lock(mutex)
        defer { pthread_mutex_unlock(mutex) }
        return parameters
    }

    /// If the queue is full (nothing is rendering) the oldest command is dropped.
    func send(_ command: PlayerCommand) {
        pthread_mutex_lock(mutex)
        defer { pthread_mutex_unlock(mutex) }
        if queueCount == Self.commandCapacity {
            queueHead = (queueHead + 1) % Self.commandCapacity
            queueCount -= 1
        }
        queue[(queueHead + queueCount) % Self.commandCapacity] = command
        queueCount += 1
    }

    /// Replaces the track. The previous one is freed here, never on the render thread: the render thread parks the
    /// track it stops using in `retiredTrack`, and the next load (which comes strictly after the pick-up) reaps it.
    func load(_ track: AudioBuffer?) {
        var reaped: AudioBuffer?
        var replaced: AudioBuffer?
        pthread_mutex_lock(mutex)
        swap(&reaped, &retiredTrack)
        swap(&replaced, &pendingTrack)  // posted but never picked up: the render thread never saw it
        pendingTrack = track
        trackDirty = true
        pthread_mutex_unlock(mutex)
        withExtendedLifetime((reaped, replaced)) {}
    }

    func status() -> PlayerStatus {
        pthread_mutex_lock(mutex)
        defer { pthread_mutex_unlock(mutex) }
        return published
    }

    // MARK: render thread (never blocks, never allocates)

    /// nil when the control side holds the lock: keep the previous snapshot. `commands` needs `commandCapacity` slots.
    func fetch(parameters out: inout PlayerParameters,
               track: inout AudioBuffer?,
               commands: UnsafeMutablePointer<PlayerCommand>) -> Fetch? {
        guard pthread_mutex_trylock(mutex) == 0 else { return nil }
        defer { pthread_mutex_unlock(mutex) }
        var result = Fetch()
        out = parameters
        while queueCount > 0 {
            commands[result.commandCount] = queue[queueHead]
            result.commandCount += 1
            queueHead = (queueHead + 1) % Self.commandCapacity
            queueCount -= 1
        }
        if trackDirty {
            swap(&retiredTrack, &track)   // old render track -> parked for the control side to free
            swap(&track, &pendingTrack)   // new track in, pending slot emptied
            trackDirty = false
            result.trackChanged = true
        }
        return result
    }

    func publish(_ status: PlayerStatus) {
        guard pthread_mutex_trylock(mutex) == 0 else { return }
        published = status
        pthread_mutex_unlock(mutex)
    }

    /// Test hook: holds the lock so a render call has to take the contended path.
    func withLockHeldForTesting(_ body: () -> Void) {
        pthread_mutex_lock(mutex)
        defer { pthread_mutex_unlock(mutex) }
        body()
    }
}

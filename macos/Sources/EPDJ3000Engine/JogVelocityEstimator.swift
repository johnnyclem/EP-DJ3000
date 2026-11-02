// Platter velocity from timestamped jog ticks.
//
// The device only sends a jog message when the platter moved (value 64 is never produced), so silence means
// "still". A message's ticks happened since the previous message, so the estimate is
//
//     ticks received after the oldest event in the window / time since that event
//
// (the oldest event is only a time reference; its own ticks belong to the interval before it). Counting ticks per
// fixed window instead would read one event too high whenever the query lands on an event, which it always does.
// The span is floored at window / 2 so a burst of events with one timestamp (USB delivers in 1 ms frames) cannot
// make a spike, and a lone event is spread over the whole window.
//
// After the last event the estimate falls linearly to exactly zero over `window`: a held-still finger reads zero
// within `window` (20 ms, inside the 30 ms target). ScratchPlayerCore fades a velocity that stops being
// refreshed over the same time, so the two agree.
//
// Resolution is one tick per window: below ~50 ticks/s (0.09x) the platter reads as bursts, not a steady crawl.

public struct JogVelocityEstimator: Sendable {
    /// Seconds.
    public static let defaultWindow = 0.020

    // 500 Hz x 20 ms is 10 events; 64 absorbs bursts. When full, new ticks merge into the newest slot, never dropped.
    private static let capacity = 64

    public let window: Double
    private var times = [Double](repeating: 0, count: JogVelocityEstimator.capacity)
    private var ticks = [Int](repeating: 0, count: JogVelocityEstimator.capacity)
    private var oldest = 0
    private var count = 0
    private var latest = -Double.infinity
    private var latestVelocity = 0.0

    public init(window: Double = JogVelocityEstimator.defaultWindow) {
        precondition(window > 0 && window.isFinite, "window must be positive")
        self.window = window
    }

    public mutating func reset() {
        oldest = 0
        count = 0
        latest = -Double.infinity
        latestVelocity = 0
    }

    /// `time` is seconds on any monotonic clock, shared with `velocity(at:)`. A time that goes backwards resets.
    public mutating func addTicks(_ delta: Int, at time: Double) {
        guard time.isFinite else { return }
        if time < latest { reset() }
        latest = time
        let cap = Self.capacity
        while count > 0 && times[oldest] <= time - window {
            oldest = (oldest + 1) % cap
            count -= 1
        }
        if count == cap {
            ticks[(oldest + count - 1) % cap] += delta
        } else {
            let slot = (oldest + count) % cap
            times[slot] = time
            ticks[slot] = delta
            count += 1
        }

        if count == 1 {
            latestVelocity = Double(ticks[oldest]) / window
        } else {
            var after = 0
            for k in 1..<count { after += ticks[(oldest + k) % cap] }
            latestVelocity = Double(after) / max(time - times[oldest], window / 2)
        }
    }

    /// Signed ticks per second (positive = clockwise). Queries must not go back before the last event.
    public func velocity(at time: Double) -> Double {
        guard time.isFinite, count > 0 else { return 0 }
        let age = max(0, time - latest)
        return age >= window ? 0 : latestVelocity * (1 - age / window)
    }

    /// `velocity(at:)` as playback speed (1.0 = 33 1/3 rpm).
    public func speed(at time: Double) -> Double {
        ScratchConstants.speed(forTicksPerSecond: velocity(at: time))
    }
}

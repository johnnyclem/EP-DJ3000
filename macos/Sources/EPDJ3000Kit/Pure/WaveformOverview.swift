// Peak envelope of a whole track for the Player tab's overview. Computed once at load time from the decoded
// samples (AudioBuffer keeps its storage internal to the Engine module).

public struct WaveformOverview: Equatable, Sendable {
    /// One value per bucket, 0...1: the largest absolute sample of either channel in that slice of the track.
    public let peaks: [Float]

    public init(peaks: [Float]) {
        self.peaks = peaks
    }

    /// `buckets` is an upper bound: a track shorter than that gets one bucket per frame.
    public init(left: [Float], right: [Float], buckets: Int) {
        let frames = min(left.count, right.count)
        let n = max(0, min(buckets, frames))
        var out = [Float](repeating: 0, count: n)
        for b in 0..<n {
            let start = b * frames / n
            let end = (b + 1) * frames / n
            var peak: Float = 0
            for i in start..<end {
                let l = abs(left[i])
                let r = abs(right[i])
                if l > peak { peak = l }
                if r > peak { peak = r }
            }
            out[b] = min(1, peak.isFinite ? peak : 0)
        }
        self.peaks = out
    }
}

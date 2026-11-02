// An immutable stereo track in planar Float32. The storage is allocated once and never resized, so the render
// thread can read it without locks for as long as it holds a reference.

public final class AudioBuffer: @unchecked Sendable {
    public let frameCount: Int
    public let sampleRate: Double
    let left: UnsafeMutablePointer<Float>
    let right: UnsafeMutablePointer<Float>

    /// Copies `frameCount` samples from each pointer (e.g. straight out of a decoded file's channel data).
    /// NaN and infinity are replaced by silence so nothing downstream can be poisoned.
    public init(left: UnsafePointer<Float>, right: UnsafePointer<Float>, frameCount: Int, sampleRate: Double) {
        precondition(frameCount >= 0, "frame count must not be negative")
        precondition(sampleRate > 0 && sampleRate.isFinite, "sample rate must be positive")
        self.frameCount = frameCount
        self.sampleRate = sampleRate
        self.left = .allocate(capacity: max(1, frameCount))
        self.right = .allocate(capacity: max(1, frameCount))
        for i in 0..<frameCount {
            self.left[i] = left[i].isFinite ? left[i] : 0
            self.right[i] = right[i].isFinite ? right[i] : 0
        }
    }

    public convenience init(left: [Float], right: [Float], sampleRate: Double) {
        precondition(left.count == right.count, "channels must have the same length")
        self.init(left: left, right: right, frameCount: left.count, sampleRate: sampleRate)
    }

    public convenience init(mono: [Float], sampleRate: Double) {
        self.init(left: mono, right: mono, sampleRate: sampleRate)
    }

    deinit {
        left.deallocate()
        right.deallocate()
    }

    public var duration: Double { Double(frameCount) / sampleRate }
}

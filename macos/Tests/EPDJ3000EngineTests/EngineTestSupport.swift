import Foundation
import XCTest
@testable import EPDJ3000Engine
import EPDJ3000Protocol

enum Sine {
    static func samples(frequency: Double, seconds: Double, sampleRate: Double = 48_000, amplitude: Float = 0.8) -> [Float] {
        (0..<Int(seconds * sampleRate)).map { Float(Double(amplitude) * sin(2 * .pi * frequency * Double($0) / sampleRate)) }
    }

    static func buffer(frequency: Double, seconds: Double, sampleRate: Double = 48_000, amplitude: Float = 0.8) -> AudioBuffer {
        AudioBuffer(mono: samples(frequency: frequency, seconds: seconds, sampleRate: sampleRate, amplitude: amplitude),
                    sampleRate: sampleRate)
    }
}

/// Drives a ScratchPlayerCore the way an audio callback would, 96 frames (2 ms, the jog cadence) at a time by default.
final class Rig {
    let core: ScratchPlayerCore
    let outputRate: Double
    var block = 96
    private var left = [Float](repeating: 0, count: 8192)
    private var right = [Float](repeating: 0, count: 8192)
    /// Right channel of everything rendered since the last `render(frames:)` call.
    private(set) var lastRight = [Float]()

    init(track: AudioBuffer?, outputRate: Double = 48_000, config: ScratchPlayerConfig = ScratchPlayerConfig()) {
        self.outputRate = outputRate
        core = ScratchPlayerCore(sampleRate: outputRate, config: config)
        core.load(track)
        render(frames: 1)  // the render thread picks the track up at the start of a block
    }

    var status: PlayerStatus { core.status }

    func frames(_ seconds: Double) -> Int { Int((seconds * outputRate).rounded()) }

    /// Renders `frames` frames and returns the left channel. If `jog` is given it is re-published before every block,
    /// the way a 500 Hz jog stream keeps refreshing it. `each` runs after every block with the frames rendered so far.
    @discardableResult
    func render(frames total: Int, jog: Double? = nil, each: ((Int) -> Void)? = nil) -> [Float] {
        var out = [Float]()
        out.reserveCapacity(total)
        lastRight = []
        var done = 0
        while done < total {
            let n = min(block, total - done)
            if let jog { core.setJog(ticksPerSecond: jog) }
            left.withUnsafeMutableBufferPointer { l in
                right.withUnsafeMutableBufferPointer { r in
                    core.render(left: l.baseAddress!, right: r.baseAddress!, frameCount: n)
                }
            }
            out.append(contentsOf: left[0..<n])
            lastRight.append(contentsOf: right[0..<n])
            done += n
            each?(done)
        }
        return out
    }

    @discardableResult
    func render(seconds: Double, jog: Double? = nil, each: ((Int) -> Void)? = nil) -> [Float] {
        render(frames: frames(seconds), jog: jog, each: each)
    }

    /// Speed (1.0 = nominal) after every block of `seconds`.
    func speedTrace(seconds: Double, jog: Double? = nil) -> [Double] {
        var trace = [Double]()
        render(frames: frames(seconds), jog: jog) { _ in trace.append(self.status.speed) }
        return trace
    }

    func playheadTrace(seconds: Double, jog: Double? = nil) -> [Double] {
        var trace = [Double]()
        render(frames: frames(seconds), jog: jog) { _ in trace.append(self.status.playheadFrames) }
        return trace
    }

    /// Gets a track playing at its nominal speed from `seconds` into it, fully settled.
    func playFrom(_ seconds: Double) {
        core.send(.seek(seconds: seconds))
        core.send(.play)
        render(seconds: 0.2)
    }
}

/// Positive-going zero crossings with linear interpolation: frequency in Hz.
func measureFrequency(_ x: [Float], sampleRate: Double) -> Double {
    var first: Double?
    var last = 0.0
    var count = 0
    for i in 1..<x.count where x[i - 1] < 0 && x[i] >= 0 {
        let pos = Double(i - 1) + Double(-x[i - 1]) / Double(x[i] - x[i - 1])
        if first == nil { first = pos }
        last = pos
        count += 1
    }
    guard let f = first, count > 1 else { return 0 }
    return Double(count - 1) * sampleRate / (last - f)
}

func maxStep(_ x: [Float]) -> Float {
    var m: Float = 0
    for i in 1..<x.count { m = max(m, abs(x[i] - x[i - 1])) }
    return m
}

func allFinite(_ x: [Float]) -> Bool { x.allSatisfy { $0.isFinite } }

/// Slope of a trace (value per sample) between two sample indices.
func slope(_ trace: [Double], from a: Int, to b: Int, perStep: Double) -> Double {
    (trace[b] - trace[a]) / (Double(b - a) * perStep)
}

struct XorShift {
    var state: UInt64
    mutating func next() -> UInt64 {
        state ^= state << 13
        state ^= state >> 7
        state ^= state << 17
        return state
    }
    /// Uniform in [0, 1).
    mutating func unit() -> Double { Double(next() >> 11) / Double(1 << 53) }
    mutating func range(_ lo: Double, _ hi: Double) -> Double { lo + (hi - lo) * unit() }
    mutating func int(_ n: Int) -> Int { Int(next() % UInt64(n)) }
}

// Audio the app can play with no file and no DAC: a plain sine for "is there sound at all", and a short
// beat loop that makes scratching, reverse, cue and pitch audible. Both go through the normal player, so the
// platter, transport and fader paths are exercised exactly as with a real track.

import Foundation

public enum TestSignal {
    public static let defaultSampleRate = 48_000.0

    public typealias Stereo = (left: [Float], right: [Float])

    public static func sine(frequency: Double = 440, seconds: Double = 10, amplitude: Float = 0.5,
                            sampleRate: Double = defaultSampleRate) -> Stereo {
        let n = max(0, Int(seconds * sampleRate))
        let w = 2 * Double.pi * frequency / sampleRate
        let samples = (0..<n).map { Float(Double(amplitude) * sin(w * Double($0))) }
        return (samples, samples)
    }

    /// Kick on every beat, hat on every off-beat, a minor-seventh arpeggio in 8ths alternating between the channels.
    /// Deterministic (the hat noise is a fixed xorshift), peak below 1.
    public static func demoLoop(bpm: Double = 120, bars: Int = 8, sampleRate: Double = defaultSampleRate) -> Stereo {
        let beatFrames = 60.0 / bpm * sampleRate
        let frames = max(0, Int(beatFrames * 4 * Double(max(1, bars)) + 0.5))
        var left = [Float](repeating: 0, count: frames)
        var right = [Float](repeating: 0, count: frames)
        let arp: [Double] = [220.0, 261.63, 329.63, 392.0]
        var noiseState: UInt32 = 0x1234_5678
        var previousNoise = 0.0

        for i in 0..<frames {
            let beatPos = Double(i) / beatFrames
            let sinceBeat = (beatPos - beatPos.rounded(.down)) * beatFrames / sampleRate   // seconds since the last beat
            let eighth = Int(beatPos * 2)
            let sinceEighth = (beatPos * 2 - Double(eighth)) * beatFrames / 2 / sampleRate

            // Kick: 45 Hz + a 90 Hz sweep that decays at 30/s; its phase is the integral of that frequency.
            var kick = 0.0
            if sinceBeat < 0.35 {
                let phase = 2 * Double.pi * (45 * sinceBeat + 90 * (1 - exp(-30 * sinceBeat)) / 30)
                kick = 0.7 * sin(phase) * exp(-9 * sinceBeat)
            }

            // Hat: high-passed noise (first difference) on the off-beat 8ths.
            var hat = 0.0
            noiseState ^= noiseState << 13
            noiseState ^= noiseState >> 17
            noiseState ^= noiseState << 5
            let noise = Double(noiseState) / Double(UInt32.max) * 2 - 1
            if eighth % 2 == 1 && sinceEighth < 0.06 {
                hat = 0.25 * (noise - previousNoise) * 0.5 * exp(-90 * sinceEighth)
            }
            previousNoise = noise

            // Arpeggio.
            let f = arp[eighth % arp.count]
            let env = exp(-7 * sinceEighth)
            let t = Double(i) / sampleRate
            let note = 0.2 * env * (sin(2 * Double.pi * f * t) + 0.3 * sin(4 * Double.pi * f * t))
            let panLeft = eighth % 2 == 0 ? 1.0 : 0.45

            left[i] = Float(tanh(kick + hat + note * panLeft))
            right[i] = Float(tanh(kick + hat + note * (1.45 - panLeft)))
        }
        return (left, right)
    }
}

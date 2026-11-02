// "Is the AS5600 working?" as a state machine fed with telemetry while the user spins the platter by hand.
// Inputs: `Info.caps` (AS5600 bit = a chip answered on I2C at boot), `Telemetry.flags` ANGLE_VALID (the read just
// worked) and `Telemetry.angle` (12 bit, 4096 per turn). Pure like the other wizards: no clock, durations are sample
// counts derived from the stream rate.
//
// It never looks at the touched flag, the hold pad or the jog stream, so it works the same whatever `jogUntouched` is.
//
// The thresholds below are reasoned, not measured: nothing here has met a real AS5600.

public struct EncoderDiagnosis: Sendable {
    public enum Verdict: Equatable, Sendable {
        /// No chip has answered (`Caps.as5600` clear; the unit keeps re-probing every 500 ms): wiring (SDA/SCL, 3.3 V, pull-ups), address, or the
        /// encoder probe is disabled in the build.
        case noEncoder
        /// A chip answered but the unit got no valid angle in the whole check.
        case readsFailing(samples: Int)
        /// Valid angles, but the platter was not turned a full revolution: no magnet, too far, or not on the axle.
        case notMoving(spanCounts: Int)
        /// Valid angles that jump around instead of following a spin: magnet far off-centre or too weak/strong, or
        /// a noisy bus.
        case erratic(jumps: Int, steps: Int)
        /// A smooth spin was measured.
        case ok(turns: Double, invalidSamples: Int)

        public var isOK: Bool {
            if case .ok = self { return true }
            return false
        }
    }

    public enum Phase: Equatable, Sendable {
        /// Waiting for / watching the spin. `spanCounts` of `requiredCounts` so far.
        case measuring(spanCounts: Int, requiredCounts: Int)
        case decided(Verdict)
    }

    public static let countsPerTurn = 4096
    /// A hand does not turn a platter faster than this; a bigger step between two telemetry samples is a glitch.
    public static let maxPlausibleTurnsPerSecond = 5.0
    /// A smooth spin needs at least this many samples before it is believed.
    public static let minSteps = 10

    public private(set) var phase: Phase
    /// Largest unwrapped excursion of the angle since the start (max minus min), in 1/4096 turns.
    public private(set) var spanCounts = 0

    private let requiredCounts: Int
    private let timeoutSamples: Int
    private let maxStep: Int
    private var samples = 0
    private var validSamples = 0
    private var steps = 0
    private var jumps = 0
    private var position = 0   // unwrapped, relative to the first valid angle
    private var low = 0
    private var high = 0
    private var lastAngle: Int?

    /// `rateHz` is the telemetry stream rate.
    public init(caps: Caps, rateHz: Int, requiredTurns: Double = 1.0, timeoutSeconds: Double = 10) {
        let rate = Double(max(1, rateHz))
        requiredCounts = Int((requiredTurns * Double(Self.countsPerTurn)).rounded())
        timeoutSamples = max(1, Int(rate * timeoutSeconds))
        // Under half a turn so the shortest way round is unambiguous.
        maxStep = min(Self.countsPerTurn / 2 - 1, Int(Double(Self.countsPerTurn) * Self.maxPlausibleTurnsPerSecond / rate))
        phase = caps.contains(.as5600) ? .measuring(spanCounts: 0, requiredCounts: requiredCounts) : .decided(.noEncoder)
    }

    public var verdict: Verdict? {
        if case let .decided(v) = phase { return v }
        return nil
    }

    public mutating func feed(_ t: Telemetry) {
        guard case .measuring = phase else { return }
        samples += 1
        if StateFlags(rawValue: t.flags).contains(.angleValid) { take(Int(t.angle) & (Self.countsPerTurn - 1)) }
        spanCounts = high - low

        if spanCounts >= requiredCounts, steps >= Self.minSteps, !isErratic {
            phase = .decided(.ok(turns: Double(spanCounts) / Double(Self.countsPerTurn), invalidSamples: samples - validSamples))
        } else if samples >= timeoutSamples {
            if validSamples == 0 {
                phase = .decided(.readsFailing(samples: samples))
            } else if isErratic {
                phase = .decided(.erratic(jumps: jumps, steps: steps))
            } else {
                phase = .decided(.notMoving(spanCounts: spanCounts))
            }
        } else {
            phase = .measuring(spanCounts: spanCounts, requiredCounts: requiredCounts)
        }
    }

    /// More than a quarter of the steps were implausibly large.
    private var isErratic: Bool { steps >= Self.minSteps && jumps * 4 > steps }

    private mutating func take(_ angle: Int) {
        defer { lastAngle = angle }
        validSamples += 1
        guard let previous = lastAngle else { return }
        var delta = angle - previous
        if delta >= Self.countsPerTurn / 2 { delta -= Self.countsPerTurn }
        if delta < -Self.countsPerTurn / 2 { delta += Self.countsPerTurn }
        steps += 1
        if abs(delta) > maxStep {   // a glitch, not motion: it must not add to the travel either
            jumps += 1
            return
        }
        position += delta
        low = min(low, position)
        high = max(high, position)
    }
}

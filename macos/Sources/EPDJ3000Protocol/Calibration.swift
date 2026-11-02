// Host-side calibration wizards as pure state machines fed with `Telemetry` (docs/PROTOCOL.md section 3.2).
// No I/O and no clocks: the app streams telemetry in, shows `phase`, and applies `configUpdates`
// with SET_CONFIG followed by SAVE_CONFIG.

public struct ConfigUpdate: Equatable, Sendable {
    public var key: ConfigKey
    public var value: Int32

    public init(key: ConfigKey, value: Int32) {
        self.key = key
        self.value = value
    }
}

public enum CalibrationError: Error, Equatable, Sendable {
    /// Pot/knob moved less than the 1500 counts the device itself demands.
    case insufficientTravel(span: Int, required: Int)
    /// Idle touch readings were too noisy to set a threshold (finger on the platter, or a floating pad).
    case unstableIdle(noisePermille: Int)
    /// A sector idles at 0 counts: pad not connected.
    case deadSector(Int)
    /// No finger was seen on this sector within the time limit.
    case noFingerDetected(sector: Int)
    /// A finger on the weakest sector rises less than the threshold the noise floor demands.
    case insufficientContrast(weakestRisePermille: Int, requiredOnPermille: Int)
    /// The platter did not turn far enough to tell which way is clockwise.
    case insufficientMotion(ticks: Int)
    /// `finish()` called before the wizard had everything it needs.
    case notComplete
}

// MARK: - Fader / knob sweep

public struct SweepResult: Equatable, Sendable {
    public var kind: CalKind
    /// Extremes actually seen in the raw 12-bit reading.
    public var rawMin: Int
    public var rawMax: Int
    public var span: Int { rawMax - rawMin }
    /// True when raw readings fall as the control moves towards its "high" end; nil if the sweep did not
    /// finish clearly at one end, so direction cannot be told.
    public var invert: Bool?
    /// Min/max inset by 1% of the span (as the on-device CAL_END does), plus the invert flag when known.
    public var configUpdates: [ConfigUpdate]
}

/// Records the extremes of `faderRaw` or `knobRaw`. The user sweeps the control from its "low" end
/// (left / down / counter-clockwise) to its "high" end and leaves it there; where it ends tells direction.
/// This mirrors the device-side CAL_BEGIN/CAL_END so the UI can show live progress and the result can be
/// cross-checked or applied by the host.
public struct SweepCalibration: Sendable {
    public enum Phase: Equatable, Sendable {
        case waitingForMotion
        case sweeping
        /// Travel is at least `minSpan`; keep going to the ends, then finish.
        case rangeReached
    }

    /// Device rejects a span below this with CAL_FAILED.
    public static let defaultMinSpan = 1500
    /// Span below which movement is indistinguishable from ADC noise (+-3 counts, with margin).
    public static let motionThreshold = 40

    public let kind: CalKind
    public let minSpan: Int
    public private(set) var minRaw: Int
    public private(set) var maxRaw: Int
    public private(set) var lastRaw: Int
    public private(set) var sampleCount = 0

    public init(kind: CalKind, minSpan: Int = SweepCalibration.defaultMinSpan) {
        self.kind = kind
        self.minSpan = minSpan
        minRaw = Int.max
        maxRaw = Int.min
        lastRaw = 0
    }

    public var span: Int { sampleCount == 0 ? 0 : maxRaw - minRaw }

    public var phase: Phase {
        if span >= minSpan { return .rangeReached }
        return span >= Self.motionThreshold ? .sweeping : .waitingForMotion
    }

    public mutating func feed(_ t: Telemetry) {
        let raw = Int(kind == .fader ? t.faderRaw : t.knobRaw)
        minRaw = min(minRaw, raw)
        maxRaw = max(maxRaw, raw)
        lastRaw = raw
        sampleCount += 1
    }

    public func finish() throws -> SweepResult {
        guard span >= minSpan else { throw CalibrationError.insufficientTravel(span: span, required: minSpan) }
        let inset = span / 100
        let lo = min(4095, minRaw + inset)
        let hi = max(0, min(4095, maxRaw - inset))

        // Direction only if the sweep ended in the outer quarter at one end.
        let mid = (minRaw + maxRaw) / 2
        var invert: Bool?
        if abs(lastRaw - mid) > span / 4 { invert = lastRaw < mid }

        let keys: (min: ConfigKey, max: ConfigKey, invert: ConfigKey) =
            kind == .fader ? (.faderRawMin, .faderRawMax, .faderInvert) : (.knobRawMin, .knobRawMax, .knobInvert)
        var updates = [ConfigUpdate(key: keys.min, value: Int32(lo)), ConfigUpdate(key: keys.max, value: Int32(hi))]
        if let invert { updates.append(ConfigUpdate(key: keys.invert, value: invert ? 1 : 0)) }
        return SweepResult(kind: kind, rawMin: minRaw, rawMax: maxRaw, invert: invert, configUpdates: updates)
    }
}

// MARK: - Touch ring

public struct TouchRingResult: Equatable, Sendable {
    public var sectors: Int
    /// Mean idle reading per sector.
    public var idleMean: [Double]
    /// Finger peak over idle baseline per sector, in per-mille of the baseline.
    public var risePermille: [Double]
    /// Worst peak-to-peak idle noise over all sectors, per-mille of baseline.
    public var noisePermille: Double
    public var onPermille: Int
    public var offPermille: Int
    public var configUpdates: [ConfigUpdate] {
        [ConfigUpdate(key: .touchOnPermille, value: Int32(onPermille)),
         ConfigUpdate(key: .touchOffPermille, value: Int32(offPermille))]
    }
}

/// Idle noise capture, then one finger per sector in order, then thresholds exactly as PROTOCOL.md 3.2:
/// `touchOnPermille` = 50% of the weakest sector's rise, `touchOffPermille` = 60% of that, neither below
/// 3x the idle noise (and on stays above off, so hysteresis survives the floor).
/// The wizard needs no button presses: it waits for each sector in turn to rise, holds, and waits for release.
public struct TouchRingCalibration: Sendable {
    public enum Phase: Equatable, Sendable {
        case capturingIdle(collected: Int, needed: Int)
        case awaitingFinger(sector: Int)
        case holding(sector: Int, collected: Int, needed: Int)
        case awaitingRelease(sector: Int)
        case done
        case failed(CalibrationError)
    }

    /// Idle noise above this (peak-to-peak, per-mille) means the pads are not at rest.
    public static let maxIdleNoisePermille = 30.0
    /// A sector counts as touched once it rises this far, or 3x idle noise if that is larger.
    public static let minContactPermille = 5.0

    public let sectors: Int
    public private(set) var phase: Phase

    private let idleNeeded: Int
    private let holdNeeded: Int
    private let fingerTimeout: Int

    private var idleSum: [Double]
    private var idleMin: [Double]
    private var idleMax: [Double]
    private var idleCount = 0

    private var mean: [Double] = []
    private var noisePermille = 0.0
    private var contactPermille = 0.0
    private var rises: [Double]
    private var waited = 0
    private var holdCollected = 0
    private var holdPeak = 0.0
    private var previousRise = 0.0
    private var quietSamples = 0

    /// `rateHz` is the telemetry stream rate; durations are converted to sample counts.
    public init(sectors: Int, rateHz: Int, idleSeconds: Double = 1.0, holdSeconds: Double = 0.5, fingerTimeoutSeconds: Double = 10) {
        let n = min(8, max(1, sectors))
        let rate = Double(max(1, rateHz))
        self.sectors = n
        idleNeeded = max(8, Int(rate * idleSeconds))
        holdNeeded = max(4, Int(rate * holdSeconds))
        fingerTimeout = max(1, Int(rate * fingerTimeoutSeconds))
        idleSum = [Double](repeating: 0, count: n)
        idleMin = [Double](repeating: .infinity, count: n)
        idleMax = [Double](repeating: -.infinity, count: n)
        rises = [Double](repeating: 0, count: n)
        phase = .capturingIdle(collected: 0, needed: idleNeeded)
    }

    public mutating func feed(_ t: Telemetry) {
        let raw = (0..<sectors).map { $0 < t.touchRaw.count ? Double(t.touchRaw[$0]) : 0 }
        switch phase {
        case .capturingIdle: feedIdle(raw)
        case let .awaitingFinger(s): feedAwaiting(s, raw)
        case let .holding(s, _, _): feedHolding(s, raw)
        case let .awaitingRelease(s): feedRelease(s, raw)
        case .done, .failed: break
        }
    }

    public func finish() throws -> TouchRingResult {
        switch phase {
        case let .failed(e): throw e
        case .done: break
        default: throw CalibrationError.notComplete
        }
        let weakest = rises.min() ?? 0
        var on = Int((0.5 * weakest).rounded())
        var off = Int((0.6 * Double(on)).rounded())
        // ceil(3 x noise); the epsilon keeps a noise of exactly 5.000000000000001 from costing a whole per-mille.
        let floor = Int((3 * noisePermille - 1e-9).rounded(.up))
        off = max(off, floor, 1)
        on = max(on, off + 1)
        guard Double(on) < weakest else {
            throw CalibrationError.insufficientContrast(weakestRisePermille: Int(weakest), requiredOnPermille: on)
        }
        return TouchRingResult(sectors: sectors, idleMean: mean, risePermille: rises, noisePermille: noisePermille,
                               onPermille: min(on, 1000), offPermille: min(off, 1000))
    }

    // MARK: phases

    private mutating func feedIdle(_ raw: [Double]) {
        for i in 0..<sectors {
            idleSum[i] += raw[i]
            idleMin[i] = min(idleMin[i], raw[i])
            idleMax[i] = max(idleMax[i], raw[i])
        }
        idleCount += 1
        guard idleCount >= idleNeeded else {
            phase = .capturingIdle(collected: idleCount, needed: idleNeeded)
            return
        }
        mean = idleSum.map { $0 / Double(idleCount) }
        if let dead = mean.firstIndex(where: { $0 <= 0 }) {
            phase = .failed(.deadSector(dead))
            return
        }
        noisePermille = (0..<sectors).map { (idleMax[$0] - idleMin[$0]) * 1000 / mean[$0] }.max() ?? 0
        if noisePermille > Self.maxIdleNoisePermille {
            phase = .failed(.unstableIdle(noisePermille: Int(noisePermille.rounded())))
            return
        }
        contactPermille = max(Self.minContactPermille, 3 * noisePermille)
        waited = 0
        phase = .awaitingFinger(sector: 0)
    }

    private func rise(_ sector: Int, _ raw: [Double]) -> Double {
        (raw[sector] - mean[sector]) * 1000 / mean[sector]
    }

    private mutating func feedAwaiting(_ s: Int, _ raw: [Double]) {
        waited += 1
        if rise(s, raw) >= contactPermille {
            holdCollected = 0
            holdPeak = 0
            previousRise = 0
            phase = .holding(sector: s, collected: 0, needed: holdNeeded)
            feedHolding(s, raw)
        } else if waited >= fingerTimeout {
            phase = .failed(.noFingerDetected(sector: s))
        }
    }

    private mutating func feedHolding(_ s: Int, _ raw: [Double]) {
        let r = rise(s, raw)
        // The lower of two consecutive samples, so a one-sample spike cannot set the peak.
        holdPeak = max(holdPeak, min(r, previousRise))
        previousRise = r
        holdCollected += 1
        guard holdCollected >= holdNeeded else {
            phase = .holding(sector: s, collected: holdCollected, needed: holdNeeded)
            return
        }
        rises[s] = holdPeak
        quietSamples = 0
        phase = .awaitingRelease(sector: s)
    }

    private mutating func feedRelease(_ s: Int, _ raw: [Double]) {
        quietSamples = rise(s, raw) < contactPermille / 2 ? quietSamples + 1 : 0
        guard quietSamples >= 3 else { return }
        if s + 1 < sectors {
            waited = 0
            phase = .awaitingFinger(sector: s + 1)
        } else {
            phase = .done
        }
    }
}

// MARK: - Platter direction

public struct PlatterDirectionResult: Equatable, Sendable {
    /// Net `jogTotal` change while the user spun the platter clockwise.
    public var netTicks: Int
    /// The `jogInvert` value that makes clockwise positive.
    public var invert: Bool
    public var configUpdates: [ConfigUpdate]
}

/// The user spins the platter clockwise; if `jogTotal` falls, the direction is wrong.
/// `jogTotal` is the sum of the jog deltas the device sends, i.e. it already includes the current `jogInvert`,
/// so a falling total means "flip the current setting", not "set it to 1".
public struct PlatterDirectionCalibration: Sendable {
    public enum Phase: Equatable, Sendable {
        case waitingForSpin
        case decided(PlatterDirectionResult)
    }

    public let currentInvert: Bool
    /// Net travel needed to decide: 128 ticks = 1/8 revolution.
    public let minTicks: Int
    public private(set) var phase: Phase = .waitingForSpin
    public private(set) var netTicks = 0
    private var baseline: Int32?

    public init(currentInvert: Bool, minTicks: Int = 128) {
        self.currentInvert = currentInvert
        self.minTicks = minTicks
    }

    public mutating func feed(_ t: Telemetry) {
        guard case .waitingForSpin = phase else { return }
        guard let base = baseline else {
            baseline = t.jogTotal
            return
        }
        netTicks = Int(t.jogTotal &- base)  // wrapping, so an i32 rollover mid-spin is harmless
        guard abs(netTicks) >= minTicks else { return }
        let invert = netTicks < 0 ? !currentInvert : currentInvert
        phase = .decided(PlatterDirectionResult(
            netTicks: netTicks,
            invert: invert,
            configUpdates: [ConfigUpdate(key: .jogInvert, value: invert ? 1 : 0)]))
    }

    public func finish() throws -> PlatterDirectionResult {
        guard case let .decided(r) = phase else { throw CalibrationError.insufficientMotion(ticks: abs(netTicks)) }
        return r
    }
}

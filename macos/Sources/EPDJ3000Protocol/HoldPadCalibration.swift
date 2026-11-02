// Hold-pad wizard for platterSource = AS5600 (and TOUCH_HOLD_ONLY): one touch pad on the platter, no rotation sensing
// from it. The firmware repeats that pad's reading across its ring detector, so the thresholds that matter are the
// ring's, for a single sector. This is the ring wizard run with one sector, not a second copy of its maths.

public struct HoldPadResult: Equatable, Sendable {
    /// Mean idle reading of `touchRaw[0]`.
    public var idleMean: Double
    /// Hand-on-platter peak over the idle baseline, in per-mille of the baseline.
    public var risePermille: Double
    /// Peak-to-peak idle noise, per-mille of the baseline.
    public var noisePermille: Double
    public var onPermille: Int
    public var offPermille: Int
    public var configUpdates: [ConfigUpdate] {
        [ConfigUpdate(key: .touchOnPermille, value: Int32(onPermille)),
         ConfigUpdate(key: .touchOffPermille, value: Int32(offPermille))]
    }
}

/// Idle noise for 1 s (hands off), then a hand on the platter, then thresholds by the same rules as the ring wizard
/// (`TouchThresholds`, PROTOCOL.md 3.2). Reads `Telemetry.touchRaw[0]` only; the other seven entries are 0 with a
/// one-pad build and are ignored.
///
/// What this cannot tell: whether the reading stays up while the platter turns under the hand. The pad's signal has
/// to cross a rotating joint; check that on the Decks tab (the touch light must stay on with the platter spinning)
/// after applying.
public struct HoldPadCalibration: Sendable {
    public enum Phase: Equatable, Sendable {
        case capturingIdle(collected: Int, needed: Int)
        case awaitingHand
        case holding(collected: Int, needed: Int)
        case awaitingRelease
        case done
        case failed(CalibrationError)
    }

    private var ring: TouchRingCalibration

    /// `rateHz` is the telemetry stream rate; durations are converted to sample counts.
    public init(rateHz: Int, idleSeconds: Double = 1.0, holdSeconds: Double = 0.5, handTimeoutSeconds: Double = 10) {
        ring = TouchRingCalibration(sectors: 1, rateHz: rateHz, idleSeconds: idleSeconds, holdSeconds: holdSeconds,
                                    fingerTimeoutSeconds: handTimeoutSeconds)
    }

    public var phase: Phase {
        switch ring.phase {
        case let .capturingIdle(collected, needed): return .capturingIdle(collected: collected, needed: needed)
        case .awaitingFinger: return .awaitingHand
        case let .holding(_, collected, needed): return .holding(collected: collected, needed: needed)
        case .awaitingRelease: return .awaitingRelease
        case .done: return .done
        case let .failed(error): return .failed(Self.padError(error))
        }
    }

    public mutating func feed(_ t: Telemetry) {
        ring.feed(t)
    }

    public func finish() throws -> HoldPadResult {
        let r: TouchRingResult
        do {
            r = try ring.finish()
        } catch let error as CalibrationError {
            throw Self.padError(error)
        }
        return HoldPadResult(idleMean: r.idleMean[0], risePermille: r.risePermille[0], noisePermille: r.noisePermille,
                             onPermille: r.onPermille, offPermille: r.offPermille)
    }

    /// The ring names a sector in these two errors; with one pad there is no sector to name.
    private static func padError(_ error: CalibrationError) -> CalibrationError {
        switch error {
        case .deadSector: return .padNotConnected
        case .noFingerDetected: return .noHandDetected
        default: return error
        }
    }
}

// Runs the protocol library's calibration state machines (Calibration.swift) against a live telemetry stream and
// applies their results with SET_CONFIG / SAVE_CONFIG. All the judgement lives in those machines; this only feeds them,
// reports their phase, and moves their output to the unit.
//
// One step runs at a time. Live values (`latest`, `sweep`, `touch`, `platter`, `sectorRise`) are plain properties for
// views to read on a timeline; `statuses` is what changes rarely and is @Published.

import Combine
import EPDJ3000KitPure
import EPDJ3000Protocol
import Foundation

@MainActor
public final class CalibrationController: ObservableObject {
    public enum Step: String, CaseIterable, Identifiable, Sendable {
        case fader, knob, touchRing, platter
        public var id: String { rawValue }
    }

    public enum Status: Equatable {
        case notStarted
        case running
        /// Measured; the result waits for Apply.
        case measured(String)
        case applied(String)
        case failed(String)
    }

    public let session: DeckSession

    @Published public var selected: Step = .fader
    @Published public private(set) var statuses: [Step: Status] = [:]
    @Published public private(set) var isSaved = false
    @Published public private(set) var isBusy = false

    public private(set) var latest: Telemetry?
    public private(set) var runningStep: Step?
    public private(set) var sweep: SweepCalibration?
    public private(set) var touch: TouchRingCalibration?
    public private(set) var platter: PlatterDirectionCalibration?
    /// Per sector: how far the live reading is above the idle average captured at the start, per-mille. Display only.
    public private(set) var sectorRise: [Double] = []

    private var pending: [ConfigUpdate] = []
    private var consumer: Task<Void, Never>?
    private var idleSum = [Double](repeating: 0, count: 8)
    private var idleCount = 0

    public init(session: DeckSession) {
        self.session = session
    }

    public func status(_ step: Step) -> Status { statuses[step] ?? .notStarted }

    public var allApplied: Bool {
        Step.allCases.allSatisfy { if case .applied = status($0) { return true } else { return false } }
    }

    // MARK: lifecycle (call from onAppear / onDisappear)

    public func attach() {
        guard consumer == nil, let client = session.client else { return }
        session.acquireTelemetry()
        let frames = client.telemetry()
        consumer = Task { [weak self] in
            for await frame in frames {
                guard let self else { return }
                self.feed(frame)
            }
        }
    }

    public func detach() {
        guard consumer != nil else { return }
        consumer?.cancel()
        consumer = nil
        session.releaseTelemetry()
        runningStep = nil
        for (step, status) in statuses where status == .running { statuses[step] = .notStarted }
    }

    // MARK: steps

    public func start(_ step: Step) {
        selected = step
        pending = []
        sweep = nil
        touch = nil
        platter = nil
        sectorRise = []
        idleSum = [Double](repeating: 0, count: 8)
        idleCount = 0
        isSaved = false
        runningStep = step
        statuses[step] = .running
        switch step {
        case .fader: sweep = SweepCalibration(kind: .fader)
        case .knob: sweep = SweepCalibration(kind: .knob)
        case .touchRing:
            let sectors = min(8, max(3, Int(session.config[.touchSectors] ?? 8)))
            touch = TouchRingCalibration(sectors: sectors, rateHz: DeckSession.telemetryRateHz)
            sectorRise = [Double](repeating: 0, count: sectors)
        case .platter:
            platter = PlatterDirectionCalibration(currentInvert: (session.config[.jogInvert] ?? 0) != 0)
        }
    }

    public func cancel() {
        if let step = runningStep { statuses[step] = .notStarted }
        runningStep = nil
        pending = []
    }

    /// Fader and knob: the user decides when the sweep is done.
    public func finishSweep() {
        guard let step = runningStep, let sweep, step == .fader || step == .knob else { return }
        do {
            let result = try sweep.finish()
            pending = result.configUpdates
            let direction: String
            switch result.invert {
            case true?: direction = "reads high at the low end: will be inverted"
            case false?: direction = "direction is already right"
            case nil: direction = "the sweep did not end at one end, so the direction setting is left alone"
            }
            statuses[step] = .measured("Raw \(result.rawMin) to \(result.rawMax) (span \(result.span)); \(direction).")
        } catch {
            statuses[step] = .failed(ErrorText.describe(error))
        }
        runningStep = nil
    }

    /// Sends the measured values to the unit (live; Save makes them permanent).
    public func apply(_ step: Step) async {
        guard case let .measured(summary) = status(step), !isBusy else { return }
        isBusy = true
        defer { isBusy = false }
        if await session.apply(pending) {
            statuses[step] = .applied(summary)
            isSaved = false
        } else {
            statuses[step] = .failed(session.notice ?? "The unit refused the new values.")
        }
    }

    public func save() async {
        guard !isBusy else { return }
        isBusy = true
        defer { isBusy = false }
        isSaved = await session.saveConfig()
    }

    // MARK: telemetry

    private func feed(_ t: Telemetry) {
        latest = t
        guard let step = runningStep else { return }
        switch step {
        case .fader, .knob:
            sweep?.feed(t)
        case .touchRing:
            guard var machine = touch else { return }
            if case .capturingIdle = machine.phase {
                for i in 0..<min(machine.sectors, t.touchRaw.count) { idleSum[i] += Double(t.touchRaw[i]) }
                idleCount += 1
            }
            machine.feed(t)
            touch = machine
            updateRise(t, sectors: machine.sectors)
            evaluateTouch(machine)
        case .platter:
            guard var machine = platter else { return }
            machine.feed(t)
            platter = machine
            if case .decided = machine.phase { evaluatePlatter(machine) }
        }
    }

    private func updateRise(_ t: Telemetry, sectors: Int) {
        guard idleCount > 0 else { return }
        sectorRise = (0..<sectors).map { i -> Double in
            let baseline = idleSum[i] / Double(idleCount)
            guard baseline > 0, i < t.touchRaw.count else { return 0 }
            return (Double(t.touchRaw[i]) - baseline) * 1000 / baseline
        }
    }

    private func evaluateTouch(_ machine: TouchRingCalibration) {
        switch machine.phase {
        case .done:
            do {
                let r = try machine.finish()
                pending = r.configUpdates
                let weakest = Int(r.risePermille.min() ?? 0)
                statuses[.touchRing] = .measured("Touch on at \(r.onPermille), off at \(r.offPermille) per-mille. Weakest sector rises \(weakest); idle noise \(Int(r.noisePermille.rounded())).")
            } catch {
                statuses[.touchRing] = .failed(ErrorText.describe(error))
            }
            runningStep = nil
        case let .failed(error):
            statuses[.touchRing] = .failed(ErrorText.describe(error))
            runningStep = nil
        default:
            break
        }
    }

    private func evaluatePlatter(_ machine: PlatterDirectionCalibration) {
        do {
            let r = try machine.finish()
            pending = r.configUpdates
            let text = r.invert == machine.currentInvert
                ? "Clockwise gave +\(abs(r.netTicks)) ticks: direction is right."
                : "Clockwise gave \(r.netTicks) ticks: jog direction will be flipped."
            statuses[.platter] = .measured(text)
        } catch {
            statuses[.platter] = .failed(ErrorText.describe(error))
        }
        runningStep = nil
    }
}

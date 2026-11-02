// Calibrate tab: a guided wizard over the protocol library's calibration state machines. The machines decide;
// this shows their phase, the live readings, and moves the result to the unit.

import EPDJ3000Kit
import EPDJ3000Protocol
import SwiftUI

typealias CalStep = CalibrationController.Step

extension CalStep {
    var title: String {
        switch self {
        case .fader: return "Slide pot"
        case .knob: return "Knob"
        case .touchRing: return "Touch ring"
        case .platter: return "Platter direction"
        }
    }

    var systemImage: String {
        switch self {
        case .fader: return "slider.horizontal.3"
        case .knob: return "dial.medium"
        case .touchRing: return "hand.point.up.left"
        case .platter: return "arrow.clockwise"
        }
    }

    var instruction: String {
        switch self {
        case .fader:
            return "Start with the slide pot at its left end. Push it slowly to the right end and leave it there. The end where you stop is taken as \"high\", which also sets the direction. It needs about 1500 counts of travel."
        case .knob:
            return "Turn the knob fully counter-clockwise, then fully clockwise, and leave it there. The end where you stop is taken as \"high\"."
        case .touchRing:
            return "Take your hands off the platter first. The app measures the idle noise, then asks for a finger on each sector in turn: press and hold until the next one is requested. Sector 1 is the first pad as wired."
        case .platter:
            return "Put a finger on the platter and slide it clockwise (as seen from above) for at least a quarter turn. If the count goes negative the jog direction gets flipped."
        }
    }
}

@MainActor
struct CalibrateView: View {
    @EnvironmentObject var manager: DeviceManager
    @State private var selectedID = ""

    private var current: DeckSession? {
        manager.decks.first { $0.id == selectedID } ?? manager.decks.first
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            if let session = current {
                DeckPicker(sessions: manager.decks, selection: $selectedID)
                CalibrationPane(session: session).id(session.id)
            } else {
                EmptyUnitView()
            }
        }
        .padding(16)
    }
}

@MainActor
struct CalibrationPane: View {
    @ObservedObject var session: DeckSession
    @StateObject private var cal: CalibrationController

    init(session: DeckSession) {
        _session = ObservedObject(wrappedValue: session)
        _cal = StateObject(wrappedValue: CalibrationController(session: session))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            NoticeBanner(text: $session.notice)
            if !session.isReady {
                Text("Waiting for the unit to answer (\(session.label)).").foregroundStyle(.secondary)
            }
            HStack(alignment: .top, spacing: 16) {
                stepList.frame(width: 240)
                ScrollView { StepDetail(cal: cal, step: cal.selected).padding(.trailing, 8) }
            }
        }
        .onAppear { cal.attach() }
        .onDisappear { cal.detach() }
    }

    private var stepList: some View {
        VStack(alignment: .leading, spacing: 8) {
            ForEach(CalStep.allCases) { step in
                Button {
                    cal.selected = step
                } label: {
                    HStack {
                        Image(systemName: step.systemImage).frame(width: 22)
                        Text(step.title)
                        Spacer()
                        StatusIcon(status: cal.status(step))
                    }
                    .padding(8)
                    .background(RoundedRectangle(cornerRadius: 8).fill(cal.selected == step ? Color.orange.opacity(0.25) : Color.clear))
                }
                .buttonStyle(.plain)
            }
            Divider()
            Button("Save to unit") { Task { await cal.save() } }
                .buttonStyle(.borderedProminent)
                .disabled(!session.isReady || cal.isBusy)
            Text(cal.isSaved ? "Saved. The values survive a power cycle." : "Applied values are live but not saved.")
                .font(.caption).foregroundStyle(.secondary)
            if cal.allApplied {
                Label("All four steps passed", systemImage: "checkmark.seal.fill").foregroundStyle(.green)
            }
        }
    }
}

@MainActor
struct StatusIcon: View {
    let status: CalibrationController.Status

    var body: some View {
        switch status {
        case .notStarted: Image(systemName: "circle").foregroundStyle(.secondary)
        case .running: ProgressView().controlSize(.small)
        case .measured: Image(systemName: "clock.badge.checkmark").foregroundStyle(.orange)
        case .applied: Image(systemName: "checkmark.circle.fill").foregroundStyle(.green)
        case .failed: Image(systemName: "xmark.octagon.fill").foregroundStyle(.red)
        }
    }
}

@MainActor
struct StepDetail: View {
    @ObservedObject var cal: CalibrationController
    let step: CalStep

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text(step.title).font(.title2)
            Text(step.instruction).foregroundStyle(.secondary)
            switch step {
            case .fader, .knob: SweepPanel(cal: cal, step: step)
            case .touchRing: TouchRingPanel(cal: cal)
            case .platter: PlatterDirectionPanel(cal: cal)
            }
            resultLine
            actions
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    @ViewBuilder private var resultLine: some View {
        switch cal.status(step) {
        case .notStarted, .running:
            EmptyView()
        case let .measured(text):
            Label(text + " Apply it to send it to the unit.", systemImage: "clock.badge.checkmark").foregroundStyle(.orange)
        case let .applied(text):
            Label("Pass. " + text, systemImage: "checkmark.circle.fill").foregroundStyle(.green)
        case let .failed(text):
            Label("Fail. " + text, systemImage: "xmark.octagon.fill").foregroundStyle(.red)
        }
    }

    private var actions: some View {
        HStack {
            switch cal.status(step) {
            case .notStarted, .failed, .applied:
                Button("Start") { cal.start(step) }.buttonStyle(.borderedProminent)
            case .running:
                if step == .fader || step == .knob {
                    Live(hz: 10) { _ in
                        Button("Finish") { cal.finishSweep() }
                            .buttonStyle(.borderedProminent)
                            .disabled(cal.sweep?.phase != .rangeReached)
                    }
                    .fixedSize()
                }
                Button("Cancel") { cal.cancel() }
            case .measured:
                Button("Apply to unit") { Task { await cal.apply(step) } }
                    .buttonStyle(.borderedProminent)
                    .disabled(cal.isBusy)
                Button("Redo") { cal.start(step) }
            }
        }
    }
}

// MARK: - panels

@MainActor
struct SweepPanel: View {
    @ObservedObject var cal: CalibrationController
    let step: CalStep

    var body: some View {
        Live(hz: 20) { _ in
            let raw = step == .fader ? Int(cal.latest?.faderRaw ?? 0) : Int(cal.latest?.knobRaw ?? 0)
            VStack(alignment: .leading, spacing: 8) {
                MeterBar(title: "Raw reading", value: Double(raw) / 4095, detail: "\(raw) / 4095")
                if cal.latest == nil {
                    Text("No telemetry yet. Start the step and wait a moment.").font(.caption).foregroundStyle(.secondary)
                }
                if let sweep = cal.sweep, cal.runningStep == step {
                    KeyValueRow("Lowest seen", "\(sweep.minRaw)")
                    KeyValueRow("Highest seen", "\(sweep.maxRaw)")
                    MeterBar(title: "Travel", value: Double(sweep.span) / Double(sweep.minSpan),
                             detail: "\(sweep.span) of \(sweep.minSpan) needed",
                             tint: sweep.span >= sweep.minSpan ? .green : .orange)
                    Text(phaseText(sweep.phase)).font(.callout)
                }
            }
        }
    }

    private func phaseText(_ phase: SweepCalibration.Phase) -> String {
        switch phase {
        case .waitingForMotion: return "Waiting for you to move it."
        case .sweeping: return "Keep going to both ends."
        case .rangeReached: return "Enough travel. Leave it at the high end and press Finish."
        }
    }
}

@MainActor
struct TouchRingPanel: View {
    @ObservedObject var cal: CalibrationController

    var body: some View {
        Live(hz: 20) { _ in
            let rises = cal.sectorRise
            HStack(alignment: .top, spacing: 18) {
                PlatterView(angle: 0, touched: false, sectorLevels: rises.map { max(0, min(1, $0 / 100)) })
                    .frame(width: 170, height: 170)
                VStack(alignment: .leading, spacing: 8) {
                    if let machine = cal.touch, cal.runningStep == .touchRing {
                        Text(phaseText(machine.phase)).font(.callout)
                        if let progress = progress(machine.phase) { ProgressView(value: progress) }
                    }
                    ForEach(0..<rises.count, id: \.self) { i in
                        let raw = i < (cal.latest?.touchRaw.count ?? 0) ? Int(cal.latest?.touchRaw[i] ?? 0) : 0
                        MeterBar(title: "Sector \(i + 1)", value: max(0, min(1, rises[i] / 100)),
                                 detail: "\(Int(rises[i]))\u{2030}  raw \(raw)")
                    }
                    if rises.isEmpty {
                        Text("Start the step to see the sectors.").font(.caption).foregroundStyle(.secondary)
                    }
                }
            }
        }
    }

    private func phaseText(_ phase: TouchRingCalibration.Phase) -> String {
        switch phase {
        case let .capturingIdle(collected, needed): return "Hands off. Measuring idle noise (\(collected)/\(needed))."
        case let .awaitingFinger(sector): return "Touch sector \(sector + 1) and hold."
        case let .holding(sector, _, _): return "Holding sector \(sector + 1)..."
        case let .awaitingRelease(sector): return "Release sector \(sector + 1)."
        case .done: return "All sectors measured."
        case let .failed(error): return ErrorText.describe(error)
        }
    }

    private func progress(_ phase: TouchRingCalibration.Phase) -> Double? {
        switch phase {
        case let .capturingIdle(collected, needed): return Double(collected) / Double(max(1, needed))
        case let .holding(_, collected, needed): return Double(collected) / Double(max(1, needed))
        default: return nil
        }
    }
}

@MainActor
struct PlatterDirectionPanel: View {
    @ObservedObject var cal: CalibrationController

    var body: some View {
        Live(hz: 20) { _ in
            VStack(alignment: .leading, spacing: 8) {
                let jog = Int(cal.latest?.jogTotal ?? 0)
                KeyValueRow("Jog total (telemetry)", "\(jog)")
                if let machine = cal.platter, cal.runningStep == .platter {
                    KeyValueRow("Net travel since start", "\(machine.netTicks) ticks")
                    MeterBar(title: "Needed to decide", value: Double(abs(machine.netTicks)) / Double(machine.minTicks),
                             detail: "\(abs(machine.netTicks)) of \(machine.minTicks)")
                }
            }
        }
    }
}

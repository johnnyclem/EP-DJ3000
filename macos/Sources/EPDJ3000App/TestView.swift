// Test tab: MIDI monitor, raw telemetry, SysEx log and bench tools.

import EPDJ3000Kit
import EPDJ3000Protocol
import SwiftUI

@MainActor
struct TestView: View {
    enum Pane: String, CaseIterable, Identifiable {
        case monitor = "MIDI monitor"
        case telemetry = "Telemetry"
        case sysex = "SysEx log"
        case tools = "Tools"
        var id: String { rawValue }
    }

    @State private var pane = Pane.monitor

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Picker("Pane", selection: $pane) {
                ForEach(Pane.allCases) { Text($0.rawValue).tag($0) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .frame(maxWidth: 520)
            switch pane {
            case .monitor: MidiMonitorView()
            case .telemetry: TelemetryView()
            case .sysex: SysExLogView()
            case .tools: ToolsView()
            }
        }
        .padding(16)
    }
}

// MARK: - MIDI monitor

@MainActor
struct MidiMonitorView: View {
    @EnvironmentObject var manager: DeviceManager
    @State private var categories = Set(MidiCategory.allCases).subtracting([.clock])
    @State private var source = ""        // "" = all
    @State private var paused = false

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 12) {
                ForEach(MidiCategory.allCases) { category in
                    Toggle(category.rawValue, isOn: Binding(
                        get: { categories.contains(category) },
                        set: { on in
                            if on { categories.insert(category) } else { categories.remove(category) }
                        }))
                        .toggleStyle(.checkbox)
                }
                Spacer()
                Picker("From", selection: $source) {
                    Text("All").tag("")
                    ForEach(manager.sessions + [manager.virtualDeck]) { Text($0.label).tag($0.id) }
                }
                .frame(width: 200)
                Button(paused ? "Resume" : "Pause") {
                    paused.toggle()
                    manager.log.isPaused = paused
                }
                Button("Clear") { manager.log.clear() }
            }
            Text("Newest first. Clock ticks of one source fold into one line.").font(.caption).foregroundStyle(.secondary)
            Live(hz: 8) { _ in
                let wanted = categories
                let from = source
                // The filter runs under the log's lock: it must not touch the log itself.
                let rows = manager.log.recent(limit: 400) { entry in
                    wanted.contains(MidiFormat.category(of: entry.event)) && (from.isEmpty || entry.source == from)
                }
                MonitorList(rows: rows.reversed(), log: manager.log)
            }
        }
        .onDisappear {
            // A paused log silently drops everything: never leave it that way behind the user's back.
            manager.log.isPaused = false
            paused = false
        }
    }
}

@MainActor
struct MonitorList: View {
    let rows: [MidiLog.Entry]
    let log: MidiLog

    var body: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 1) {
                ForEach(rows) { row in
                    Text(line(row))
                        .font(.system(.caption, design: .monospaced))
                        .foregroundStyle(row.direction == .tx ? Color.orange : Color.primary)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
            .padding(8)
        }
        .background(RoundedRectangle(cornerRadius: 8).fill(Color(nsColor: .textBackgroundColor)))
        .frame(maxHeight: .infinity)
    }

    private func line(_ row: MidiLog.Entry) -> String {
        let cable = row.cable.isEmpty ? "" : " [\(row.cable)]"
        let repeats = row.repeatCount > 1 ? "  x\(row.repeatCount)" : ""
        return String(format: "%10.3f  ", row.time) + "\(row.direction.rawValue)  \(log.name(forSource: row.source))\(cable)  "
            + MidiFormat.describe(row.event) + repeats
    }
}

// MARK: - telemetry

@MainActor
struct TelemetryView: View {
    @EnvironmentObject var manager: DeviceManager
    @State private var selectedID = ""

    private var current: DeckSession? {
        manager.decks.first { $0.id == selectedID } ?? manager.decks.first
    }

    var body: some View {
        if let session = current {
            VStack(alignment: .leading, spacing: 10) {
                DeckPicker(sessions: manager.decks, selection: $selectedID)
                TelemetryPane(session: session).id(session.id)
            }
        } else {
            EmptyUnitView()
        }
    }
}

@MainActor
struct TelemetryPane: View {
    @ObservedObject var session: DeckSession
    @State private var streaming = false

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Toggle("Stream telemetry at \(DeckSession.telemetryRateHz) Hz", isOn: Binding(
                    get: { streaming },
                    set: { on in
                        guard on != streaming else { return }
                        streaming = on
                        if on { session.acquireTelemetry() } else { session.releaseTelemetry() }
                    }))
                Live(hz: 2) { _ in
                    Text(String(format: "%.0f frames/s", session.telemetryHz)).font(.caption).monospacedDigit().foregroundStyle(.secondary)
                }
            }
            NoticeBanner(text: $session.notice)
            Live(hz: 20) { _ in
                if let t = session.telemetry {
                    TelemetryTable(t: t)
                } else {
                    Text(streaming ? "Waiting for the first frame..." : "Streaming is off.").foregroundStyle(.secondary)
                }
            }
            Spacer(minLength: 0)
        }
        .onDisappear {
            if streaming {
                streaming = false
                session.releaseTelemetry()
            }
        }
    }
}

@MainActor
struct TelemetryTable: View {
    let t: Telemetry

    var body: some View {
        HStack(alignment: .top, spacing: 24) {
            VStack(alignment: .leading, spacing: 4) {
                KeyValueRow("seq", "\(t.seq)")
                KeyValueRow("buttons", Format.bits(t.buttons, names: ["CUE", "REV"]))
                KeyValueRow("flags", Format.bits(t.flags, names: ["TOUCHED", "ANGLE_VALID", "LINK_UP", "USB_MOUNTED", "AUDIO_STREAMING", "DAC_PRESENT", "BRIDGING"]))
                KeyValueRow("transport", TransportState(rawValue: t.transport).map { "\($0)" } ?? "\(t.transport)")
                KeyValueRow("fader raw / out", "\(t.faderRaw) / \(t.faderOut)")
                KeyValueRow("knob raw / out", "\(t.knobRaw) / \(t.knobOut)")
                KeyValueRow("angle", "\(t.angle)")
                KeyValueRow("bpm x10", "\(t.bpmX10)")
                KeyValueRow("jog total", "\(t.jogTotal)")
                Text(MidiFormat.hex(t.serialized(), limit: 40))
                    .font(.system(.caption2, design: .monospaced)).foregroundStyle(.secondary).textSelection(.enabled)
            }
            .frame(width: 340)
            VStack(alignment: .leading, spacing: 6) {
                Text("Touch pads (raw, bars relative to the largest)").font(.subheadline)
                let largest = Double(max(1, t.touchRaw.max() ?? 1))
                ForEach(0..<8, id: \.self) { i in
                    MeterBar(title: "Pad \(i + 1)", value: Double(t.touchRaw[i]) / largest, detail: "\(t.touchRaw[i])")
                }
            }
            .frame(maxWidth: 360)
        }
    }
}

// MARK: - SysEx log

@MainActor
struct SysExLogView: View {
    @EnvironmentObject var manager: DeviceManager

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Every SysEx frame in either direction, decoded. Telemetry frames are counted, not listed.")
                    .font(.caption).foregroundStyle(.secondary)
                Spacer()
                Button("Clear") { manager.log.clear() }
            }
            Live(hz: 8) { _ in
                let rows = manager.log.recent(limit: 300) { entry in
                    if case .sysex = entry.event { return true } else { return false }
                }
                MonitorList(rows: rows.reversed(), log: manager.log)
            }
        }
    }
}

// MARK: - tools

@MainActor
struct ToolsView: View {
    @EnvironmentObject var manager: DeviceManager
    @State private var selectedID = ""

    private var current: DeckSession? {
        manager.decks.first { $0.id == selectedID } ?? manager.decks.first
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                if let session = current {
                    DeckPicker(sessions: manager.decks, selection: $selectedID)
                    BenchTools(session: session).id(session.id)
                } else {
                    EmptyUnitView()
                }
                LatencyNote(session: current)
            }
        }
    }
}

@MainActor
struct BenchTools: View {
    @ObservedObject var session: DeckSession
    @State private var toneHz = 440.0
    @State private var attenuation = 12.0
    @State private var toneOn = false

    var body: some View {
        Card("Bench tools: \(session.label)") {
            NoticeBanner(text: $session.notice)
            HStack {
                Button("Identify (flash LED for 3 s)") { Task { await session.identify(seconds: 3) } }
                Text("Tells you which physical unit this is.").font(.caption).foregroundStyle(.secondary)
            }
            Divider()
            Text("Test tone from the unit's own DAC").font(.subheadline)
            HStack {
                Slider(value: $toneHz, in: 100...4000, step: 10)
                    .frame(width: 220)
                Text("\(Int(toneHz)) Hz").monospacedDigit().frame(width: 70, alignment: .leading)
                Stepper("Attenuation \(Int(attenuation)) dB", value: $attenuation, in: 0...60, step: 3)
                    .frame(width: 220)
            }
            HStack {
                Button(toneOn ? "Stop tone" : "Send test tone") {
                    toneOn.toggle()
                    Task { await session.deviceTone(on: toneOn, frequency: UInt16(toneHz), attenuationDb: UInt8(attenuation)) }
                }
                .buttonStyle(.borderedProminent)
                Text("Plays on the DAC output and stops by itself after 10 s. A unit without a DAC refuses it (not supported).")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
        .disabled(!session.isReady)
    }
}

@MainActor
struct LatencyNote: View {
    let session: DeckSession?

    var body: some View {
        Card("Latency") {
            if let session {
                Live(hz: 2) { _ in
                    let d = session.inputDelayMs
                    KeyValueRow("App handling delay, last second (average / worst)", String(format: "%.2f ms / %.2f ms", d.average, d.max))
                }
            }
            Text("The figure above is only the time between CoreMIDI receiving a packet and this app handling it. The full path from platter to sound adds the unit's 2 ms jog period, USB framing (about 1 ms), and the audio output buffer (often 5 to 12 ms); it cannot be measured from software alone and has not been measured on hardware.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }
}

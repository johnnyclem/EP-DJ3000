// Decks tab: each connected unit with its platter, controls and tempo/transport, live.

import EPDJ3000Engine
import EPDJ3000Kit
import EPDJ3000KitPure
import EPDJ3000Protocol
import SwiftUI

@MainActor
struct DecksView: View {
    @EnvironmentObject var manager: DeviceManager

    var body: some View {
        if manager.decks.isEmpty {
            EmptyUnitView(message: "Connect an EP-DJ3000 over USB-C. It appears here by itself. The Player tab has an on-screen deck you can try without hardware.")
        } else {
            ScrollView {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 440), spacing: 16, alignment: .top)], alignment: .leading, spacing: 16) {
                    ForEach(manager.decks) { session in
                        DeckCardView(session: session)
                    }
                }
                .padding(16)
            }
        }
    }
}

@MainActor
struct DeckCardView: View {
    @ObservedObject var session: DeckSession
    @State private var bpmText = ""

    var body: some View {
        Card(session.label) {
            header
            NoticeBanner(text: $session.notice)
            if case let .failed(why) = session.phase {
                Text(why).foregroundStyle(.red)
                Button("Try again") { session.retry() }
            }
            Live(hz: 60) { _ in liveControls }
            Divider()
            sendControls
        }
    }

    private var header: some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(session.unitName).font(.callout)
                if let info = session.info {
                    Text("Firmware \(info.fwMajor).\(info.fwMinor).\(info.fwPatch), serial \(String(info.serial, radix: 16, uppercase: true))")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
            Spacer()
            switch session.phase {
            case .connecting: ProgressView().controlSize(.small)
            case .ready: LedLabel(title: "Connected", on: true, color: .green)
            case .failed: LedLabel(title: "Not responding", on: true, color: .red)
            }
        }
    }

    private var liveControls: some View {
        let state = session.model.controller
        let speed = session.model.platterSpeed(at: HostClock.now())
        return HStack(alignment: .top, spacing: 16) {
            VStack(spacing: 8) {
                PlatterView(angle: state.jogAngleRadians, touched: state.touched)
                    .frame(width: 200, height: 200)
                Text(String(format: "%+.2f x  (%d ticks)", speed, state.jogTicks))
                    .font(.caption).monospacedDigit().foregroundStyle(.secondary)
            }
            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 14) {
                    LedLabel(title: "TOUCH", on: state.touched)
                    LedLabel(title: "CUE", on: state.buttons.contains(.cue))
                    LedLabel(title: "REV", on: state.buttons.contains(.rev))
                }
                MeterBar(title: faderTitle(state.faderRole), value: state.faderNormalized, detail: "\(state.fader)")
                MeterBar(title: "Knob", value: state.knobNormalized, detail: "\(state.knob)")
                Divider()
                KeyValueRow("Tempo", state.bpm.map { String(format: "%.1f BPM", $0) } ?? "-")
                KeyValueRow("Transport", "\(state.transport)")
                KeyValueRow("Position", String(format: "%.2f beats", state.beats))
                KeyValueRow("Time code", state.timecode.map { "\($0)" } ?? "-")
                KeyValueRow("Taps / play pulses", "\(state.tapCount) / \(state.playPulseCount)")
            }
            .frame(maxWidth: .infinity)
        }
    }

    private func faderTitle(_ role: FaderRole?) -> String {
        switch role {
        case .crossfader?: return "Slide pot: crossfader"
        case .channelVolume?: return "Slide pot: channel volume"
        case .pitch?: return "Slide pot: pitch"
        case nil: return "Slide pot (move it)"
        }
    }

    private var sendControls: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Send to the unit").font(.subheadline).foregroundStyle(.secondary)
            HStack {
                Button("Play") { session.sendTransport(.playing) }
                Button("Pause") { session.sendTransport(.paused) }
                Button("Stop") { session.sendTransport(.stopped) }
                Divider().frame(height: 18)
                Button("-0.1") { session.nudgeBpm(steps: -1) }
                Button("+0.1") { session.nudgeBpm(steps: 1) }
                TextField("BPM", text: $bpmText)
                    .frame(width: 70)
                    .onSubmit { sendBpm() }
                Button("Set BPM") { sendBpm() }
            }
            Toggle("Light the unit's PLAY and CUE LEDs from the player", isOn: $session.ledFeedback)
                .font(.caption)
            Text("The LEDs follow the player in every mode: the unit only lights them for those notes, it never starts its clock from them. Transport buttons and tempo are acted on only by a unit in sync-master mode (Settings > Mode).")
                .font(.caption).foregroundStyle(.secondary)
        }
        .disabled(!session.isReady)
    }

    private func sendBpm() {
        guard let bpm = Double(bpmText.trimmingCharacters(in: .whitespaces)), bpm.isFinite else { return }
        session.sendBpm(x10: Int((bpm * 10).rounded()))
    }
}

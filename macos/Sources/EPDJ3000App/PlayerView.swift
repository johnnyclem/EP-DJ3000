// Player tab: load a track (or a generated test signal), then drive it from the buttons here and from the unit's
// platter, buttons, fader and knob at the same time. Output can be pinned to the deck's own USB audio device.

import AppKit
import EPDJ3000Engine
import EPDJ3000Kit
import EPDJ3000KitPure
import EPDJ3000Protocol
import SwiftUI
import UniformTypeIdentifiers

@MainActor
struct PlayerView: View {
    @EnvironmentObject var manager: DeviceManager
    @State private var selectedID = "virtual"

    private var current: DeckSession {
        manager.players.first { $0.id == selectedID } ?? manager.virtualDeck
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                DeckPicker(sessions: manager.players, selection: $selectedID)
                PlayerDeckView(session: current, audio: current.audio).id(current.id)
            }
            .padding(16)
        }
    }
}

@MainActor
struct PlayerDeckView: View {
    @ObservedObject var session: DeckSession
    @ObservedObject var audio: DeckAudio
    @EnvironmentObject var manager: DeviceManager
    @State private var dropTargeted = false
    @State private var volume = 1.0
    @State private var pitchRange = ScratchConstants.pitchRange
    @State private var manualPitch = 0.0
    @State private var trackBpmText = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            NoticeBanner(text: $session.notice)
            trackCard
            if let track = audio.track {
                WaveformView(overview: track.overview, player: audio.player)
            }
            transportCard
            HStack(alignment: .top, spacing: 14) {
                tempoCard
                outputCard
            }
            platterCard
        }
    }

    // MARK: track

    private var trackCard: some View {
        Card("Track") {
            HStack {
                Button("Open audio file...") { openPanel() }
                Button("Demo loop") { audio.loadSignal(.demoLoop) }
                Button("440 Hz tone") { audio.loadSignal(.sine440) }
                if audio.isLoading {
                    ProgressView().controlSize(.small)
                    Text("Loading...").foregroundStyle(.secondary)
                }
            }
            if let track = audio.track {
                Text(track.name).font(.title3)
                Text("\(Format.clock(track.duration))  \u{00B7}  \(track.source)  \u{00B7}  played at 48 kHz stereo")
                    .font(.caption).foregroundStyle(.secondary)
            } else {
                Text("Drop an audio file here, or use the buttons. The demo loop and the tone need no file and no DAC.")
                    .foregroundStyle(.secondary)
            }
            if let message = audio.message {
                Text(message).foregroundStyle(.red).font(.callout)
            }
        }
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color.orange, lineWidth: dropTargeted ? 3 : 0))
        .onDrop(of: [UTType.fileURL], isTargeted: $dropTargeted) { providers in
            handleDrop(providers)
        }
    }

    private func openPanel() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.audio]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        if panel.runModal() == .OK, let url = panel.url { audio.load(url: url) }
    }

    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        let type = UTType.fileURL.identifier
        guard let provider = providers.first(where: { $0.hasItemConformingToTypeIdentifier(type) }) else { return false }
        let audio = self.audio
        provider.loadItem(forTypeIdentifier: type, options: nil) { item, _ in
            var url: URL?
            if let data = item as? Data {
                url = URL(dataRepresentation: data, relativeTo: nil)
            } else if let direct = item as? URL {
                url = direct
            }
            guard let url else { return }
            Task { @MainActor in audio.load(url: url) }
        }
        return true
    }

    // MARK: transport

    private var transportCard: some View {
        Card("Transport") {
            Live(hz: 30) { _ in
                let status = audio.player.status
                let state = session.model.controller
                VStack(alignment: .leading, spacing: 10) {
                    HStack(spacing: 12) {
                        Button {
                            audio.ensureEngine()
                            audio.player.send(.togglePlay)
                        } label: {
                            Label(status.transport == .playing ? "Pause" : "Play",
                                  systemImage: status.transport == .playing ? "pause.fill" : "play.fill")
                                .frame(minWidth: 80)
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(!status.hasTrack)
                        Button("Stop") { audio.player.send(.stop) }.disabled(!status.hasTrack)
                        HoldButton(title: "CUE", lit: status.cueLit,
                                   onPress: { audio.player.send(.cueDown) },
                                   onRelease: { audio.player.send(.cueUp) })
                        HoldButton(title: "REV", lit: state.buttons.contains(.rev),
                                   onPress: { audio.player.update { $0.reverseHeld = true } },
                                   onRelease: { audio.player.update { $0.reverseHeld = false } })
                        Spacer()
                        Text("\(Format.clock(status.playheadSeconds)) / \(Format.clock(status.durationSeconds))")
                            .font(.title3).monospacedDigit()
                    }
                    HStack(spacing: 18) {
                        LedLabel(title: "Playing", on: status.transport == .playing, color: .green)
                        LedLabel(title: "Cue preview", on: status.previewing)
                        LedLabel(title: "Platter touched", on: status.touched)
                        Text(String(format: "speed %+.2fx", status.speed)).font(.caption).monospacedDigit().foregroundStyle(.secondary)
                        Text("cue at \(Format.clock(status.cueSeconds))").font(.caption).monospacedDigit().foregroundStyle(.secondary)
                    }
                    if !session.isVirtual {
                        HStack(spacing: 18) {
                            MeterBar(title: "Unit fader", value: state.faderNormalized, detail: "\(state.fader)")
                            MeterBar(title: "Unit knob (volume)", value: state.knobNormalized, detail: "\(state.knob)")
                        }
                    }
                }
            }
            Text(session.isVirtual
                 ? "Use the buttons, or drag the on-screen platter below. CUE: while playing it returns to the cue point; while paused it sets the cue here and previews while held."
                 : "The unit drives this player too: touch the platter to hold the record and slide to scratch, CUE and REV work as on a CDJ, a quick tap toggles play, and the slide pot is the pitch fader (or sets the channel volume, or cuts like a crossfader, by its role).")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    // MARK: tempo and volume

    private var tempoCard: some View {
        Card("Tempo and volume") {
            Live(hz: 10) { _ in
                let model = session.model
                KeyValueRow("Pitch", String(format: "%+.2f %%", model.pitchPercent))
                KeyValueRow("Deck tempo", model.bpm.map { String(format: "%.1f BPM", $0) } ?? "unknown (enter the track's BPM)")
            }
            HStack {
                Text("Pitch range")
                Picker("Pitch range", selection: Binding(
                    get: { pitchRange },
                    set: { new in
                        pitchRange = new
                        session.model.pitchRange = new
                    })) {
                    ForEach([0.06, 0.08, 0.10, 0.16], id: \.self) { Text("\u{00B1}\(Int(($0 * 100).rounded())) %").tag($0) }
                }
                .labelsHidden()
                .frame(width: 100)
            }
            HStack {
                Text("On-screen pitch")
                Slider(value: Binding(
                    get: { manualPitch },
                    set: { new in
                        manualPitch = new
                        session.model.setTempoRatio(1 + new * pitchRange)
                    }), in: -1...1)
                Button("0") {
                    manualPitch = 0
                    session.model.setTempoRatio(1)
                }
            }
            HStack {
                Text("Track BPM")
                TextField("e.g. 124", text: $trackBpmText)
                    .frame(width: 80)
                    .onSubmit { session.model.trackBpm = Double(trackBpmText.trimmingCharacters(in: .whitespaces)) }
            }
            HStack {
                Text("Volume")
                Slider(value: Binding(
                    get: { volume },
                    set: { new in
                        volume = new
                        audio.player.update { $0.volume = Float(new) }
                    }), in: 0...1)
            }
            if !session.isVirtual, case let .choice(roles) = ConfigPresentation.kind(for: .faderRole) {
                HStack {
                    Text("Unit slide pot is")
                    Picker("Slide pot role", selection: Binding(
                        get: { session.config[.faderRole] ?? ConfigTable.desc(.faderRole).defaultValue },
                        set: { new in Task { await session.setConfig(.faderRole, new) } })) {
                        ForEach(roles, id: \.value) { Text($0.label).tag($0.value) }
                    }
                    .labelsHidden()
                    .frame(width: 170)
                }
            }
            Text("A factory unit's slide pot is the pitch fader. It and the knob override the on-screen controls when they move; choose Channel volume to cut this deck like a fader instead. The role is live; Settings > Save to unit keeps it.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    // MARK: output

    private var outputCard: some View {
        Card("Output") {
            Picker("Output device", selection: Binding(get: { audio.output }, set: { audio.setOutput($0) })) {
                Text("System default").tag(DeckAudio.Output.systemDefault)
                if case let .device(uid) = audio.output, !manager.audioDevices.contains(where: { $0.uid == uid }) {
                    Text("(disconnected device)").tag(audio.output)
                }
                ForEach(manager.audioDevices) { device in
                    Text(device.name).tag(DeckAudio.Output.device(uid: device.uid))
                }
            }
            HStack {
                Button("Refresh list") { manager.refreshAudioDevices() }
                if !session.isVirtual, let device = manager.audioDevice(for: session.deck) {
                    Button("Use \(device.name)") { audio.setOutput(.device(uid: device.uid)) }
                }
            }
            Text(audio.isEngineRunning ? "Audio engine running." : "Audio engine idle: it starts when a track is loaded.")
                .font(.caption).foregroundStyle(.secondary)
            if !session.isVirtual && manager.audioDevice(for: session.deck) == nil {
                Text("macOS lists no audio output for \(session.label). That needs a DAC fitted and dacMode not OFF.")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
    }

    // MARK: on-screen platter

    @ViewBuilder private var platterCard: some View {
        if session.isVirtual {
            Card("On-screen platter") {
                MousePlatter(session: session)
                Text("Press and drag around the centre. It sends the same touch and jog messages the unit does.")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
    }
}

/// A platter you scratch with the mouse: touch down on press, jog ticks from the angle you sweep.
@MainActor
struct MousePlatter: View {
    @ObservedObject var session: DeckSession
    @State private var lastAngle: Double?
    @State private var remainder = 0.0

    var body: some View {
        GeometryReader { geometry in
            Live(hz: 60) { _ in
                let state = session.model.controller
                PlatterView(angle: state.jogAngleRadians, touched: state.touched)
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in drag(to: value.location, in: geometry.size) }
                    .onEnded { _ in release() }
            )
        }
        .aspectRatio(1, contentMode: .fit)
        .frame(maxWidth: 260)
    }

    private func drag(to point: CGPoint, in size: CGSize) {
        let dx = Double(point.x - size.width / 2)
        let dy = Double(point.y - size.height / 2)
        guard dx * dx + dy * dy > 16 else { return }   // on the spindle the angle is noise
        let angle = atan2(dx, -dy)                      // 0 = up, clockwise positive, like the jog stream
        if let previous = lastAngle {
            var delta = angle - previous
            if delta > Double.pi { delta -= 2 * Double.pi } else if delta < -Double.pi { delta += 2 * Double.pi }
            let exact = delta / (2 * Double.pi) * Double(Proto.jogTicksPerRev) + remainder
            let ticks = Int(exact.rounded(.towardZero))
            remainder = exact - Double(ticks)
            if ticks != 0 { session.virtualJog(ticks: ticks) }
        } else {
            session.virtualTouch(true)
        }
        lastAngle = angle
    }

    private func release() {
        if lastAngle != nil { session.virtualTouch(false) }
        lastAngle = nil
        remainder = 0
    }
}

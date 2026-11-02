// Rig tab: two decks and the mixer as one system (docs/ARCHITECTURE.md section 4). Shows both units, the radio link
// each one sees (LINK_STATUS), a checklist judged from what the units and macOS report, and per-deck audio tests.

import EPDJ3000Kit
import EPDJ3000Protocol
import SwiftUI

@MainActor
struct RigView: View {
    @EnvironmentObject var manager: DeviceManager

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                if let error = manager.midiError {
                    Label(error, systemImage: "exclamationmark.triangle.fill").foregroundStyle(.red)
                }
                HStack(alignment: .top, spacing: 14) {
                    RigSlot(deck: .a)
                    RigSlot(deck: .b)
                }
                linkCard
                checklistCard
            }
            .padding(16)
        }
        .task { await refreshLoop() }
    }

    /// Link status is a request/response, so ask once a second while this tab is open.
    private func refreshLoop() async {
        while !Task.isCancelled {
            for session in manager.decks { await session.refreshLink() }
            manager.refreshAudioDevices()
            try? await Task.sleep(nanoseconds: 1_000_000_000)
        }
    }

    private var linkCard: some View {
        Card("Radio link (LINK_STATUS)") {
            if manager.decks.isEmpty {
                Text("No unit to ask.").foregroundStyle(.secondary)
            }
            ForEach(manager.decks) { session in
                LinkRows(session: session)
            }
            Text("Each deck lists the other decks it hears. A deck with no USB host of its own shows up here as riding on its partner's USB (one-cable mode).")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    private var checklistCard: some View {
        Card("Checklist") {
            Live(hz: 1) { _ in
                let checks = RigChecklist.evaluate(units: manager.decks.map(\.rigUnit),
                                                   audioDeviceNames: manager.audioDevices.map(\.name))
                VStack(alignment: .leading, spacing: 10) {
                    ForEach(checks) { check in
                        HStack(alignment: .top, spacing: 10) {
                            Image(systemName: icon(check.status)).foregroundStyle(color(check.status)).frame(width: 20)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(check.title).fontWeight(.semibold)
                                Text(check.detail).font(.callout).foregroundStyle(.secondary)
                            }
                        }
                    }
                }
            }
        }
    }

    private func icon(_ status: RigCheck.Status) -> String {
        switch status {
        case .ok: return "checkmark.circle.fill"
        case .warning: return "exclamationmark.triangle.fill"
        case .problem: return "xmark.octagon.fill"
        case .info: return "info.circle"
        }
    }

    private func color(_ status: RigCheck.Status) -> Color {
        switch status {
        case .ok: return .green
        case .warning: return .orange
        case .problem: return .red
        case .info: return .secondary
        }
    }
}

/// One deck position: the unit in it, or an empty slot.
@MainActor
struct RigSlot: View {
    let deck: Deck
    @EnvironmentObject var manager: DeviceManager

    var body: some View {
        if let session = manager.session(for: deck) {
            RigDeckCard(session: session)
        } else {
            Card("Deck \(deck == .a ? "A" : "B")") {
                Text("Not on USB.").foregroundStyle(.secondary)
                Text("If this deck is powered from a USB charger or battery and linked by radio, it works through the other deck's USB cable and is listed under the radio link instead.")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
    }
}

@MainActor
struct RigDeckCard: View {
    @ObservedObject var session: DeckSession
    @EnvironmentObject var manager: DeviceManager

    var body: some View {
        Card(session.label) {
            NoticeBanner(text: $session.notice)
            Live(hz: 2) { _ in
                let caps = session.caps
                let flags = session.telemetry.map { StateFlags(rawValue: $0.flags) }
                VStack(alignment: .leading, spacing: 6) {
                    KeyValueRow("Unit", session.unitName)
                    if let info = session.info {
                        KeyValueRow("Firmware", "\(info.fwMajor).\(info.fwMinor).\(info.fwPatch)  (protocol \(info.protocolVersion))")
                    }
                    HStack(spacing: 14) {
                        LedLabel(title: "DAC", on: caps.contains(.dac), color: .green)
                        LedLabel(title: "USB audio", on: caps.contains(.usbAudio), color: .green)
                        LedLabel(title: "TRS MIDI", on: caps.contains(.trsMidi), color: .green)
                        LedLabel(title: "Link", on: caps.contains(.link), color: .green)
                    }
                    if let flags {
                        HStack(spacing: 14) {
                            LedLabel(title: "Link up", on: flags.contains(.linkUp), color: .green)
                            LedLabel(title: "Bridging", on: flags.contains(.bridging))
                            LedLabel(title: "Audio streaming", on: flags.contains(.audioStreaming), color: .green)
                        }
                    }
                    KeyValueRow("macOS audio output", manager.audioDevice(for: session.deck)?.name ?? "none listed")
                }
            }
            Divider()
            HStack {
                Button("Identify") { Task { await session.identify() } }
                Button("Unit tone") { Task { await session.deviceTone(on: true) } }
                Button("Mac tone to this deck") { macTone() }
                    .disabled(manager.audioDevice(for: session.deck) == nil)
            }
            Text("Unit tone: sine from the deck's own DAC (10 s). Mac tone: the app's player sends 440 Hz through this deck's USB audio, which should arrive on its mixer channel.")
                .font(.caption).foregroundStyle(.secondary)
        }
        .disabled(!session.isReady)
        .onAppear { session.acquireTelemetry() }
        .onDisappear { session.releaseTelemetry() }
    }

    private func macTone() {
        guard let device = manager.audioDevice(for: session.deck) else { return }
        session.audio.setOutput(.device(uid: device.uid))
        session.audio.loadSignal(.sine440, thenPlay: true)
    }
}

@MainActor
struct LinkRows: View {
    @ObservedObject var session: DeckSession

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("\(session.label) sees").font(.subheadline)
            if !session.peersKnown {
                Text("Asking...").foregroundStyle(.secondary)
            } else if session.peers.isEmpty {
                Text("no other unit").foregroundStyle(.secondary)
            }
            ForEach(Array(session.peers.enumerated()), id: \.offset) { _, peer in
                HStack(spacing: 14) {
                    Text(peerName(peer)).frame(width: 90, alignment: .leading)
                    Text("\(peer.rssi) dBm").monospacedDigit()
                    Text(peer.rttUs > 0 ? "rtt \(peer.rttUs) us" : "rtt -").monospacedDigit()
                    Text("offset \(peer.offsetUs) us").monospacedDigit()
                    Text(flags(peer))
                }
                .font(.callout)
            }
        }
    }

    private func peerName(_ peer: Peer) -> String {
        switch peer.deck {
        case 0: return "Deck A"
        case 1: return "Deck B"
        case 0xFF: return "mixer"
        default: return "deck \(peer.deck)"
        }
    }

    private func flags(_ peer: Peer) -> String {
        let f = PeerFlags(rawValue: peer.flags)
        var parts = [String]()
        if f.contains(.usbHost) { parts.append("USB host") }
        if f.contains(.forwarding) { parts.append("forwarding") }
        if f.contains(.isMaster) { parts.append("master") }
        if peer.reserved & 1 != 0 { parts.append("DECK ID CONFLICT") }
        return parts.isEmpty ? "-" : parts.joined(separator: ", ")
    }
}

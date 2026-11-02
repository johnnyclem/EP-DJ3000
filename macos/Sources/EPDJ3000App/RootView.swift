// The window: a strip showing what is connected, and the six tabs.

import EPDJ3000Kit
import SwiftUI

@MainActor
struct RootView: View {
    var body: some View {
        VStack(spacing: 0) {
            StatusStrip()
            Divider()
            TabView {
                DecksView().tabItem { Label("Decks", systemImage: "opticaldisc") }
                CalibrateView().tabItem { Label("Calibrate", systemImage: "slider.horizontal.3") }
                TestView().tabItem { Label("Test", systemImage: "waveform.path.ecg") }
                PlayerView().tabItem { Label("Player", systemImage: "play.circle") }
                RigView().tabItem { Label("Rig", systemImage: "link") }
                SettingsView().tabItem { Label("Settings", systemImage: "gearshape") }
            }
        }
    }
}

@MainActor
struct StatusStrip: View {
    @EnvironmentObject var manager: DeviceManager

    var body: some View {
        HStack(spacing: 16) {
            if manager.decks.isEmpty {
                LedLabel(title: "No EP-DJ3000 found", on: false)
            }
            ForEach(manager.decks) { session in
                StatusChip(session: session)
            }
            if !manager.conflictingDecks.isEmpty {
                Label("Two units claim the same deck", systemImage: "exclamationmark.triangle.fill").foregroundStyle(.red)
            }
            if let error = manager.midiError {
                Label(error, systemImage: "exclamationmark.triangle.fill").foregroundStyle(.red)
            }
            Spacer()
        }
        .font(.callout)
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
    }
}

@MainActor
struct StatusChip: View {
    @ObservedObject var session: DeckSession

    var body: some View {
        switch session.phase {
        case .connecting:
            HStack(spacing: 6) { ProgressView().controlSize(.small); Text("\(session.label) connecting") }
        case .ready:
            LedLabel(title: session.label, on: true, color: .green)
        case .failed:
            LedLabel(title: "\(session.label) not responding", on: true, color: .red)
        }
    }
}

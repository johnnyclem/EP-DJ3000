// Settings tab: an editor generated from ConfigTable, one row per key. Changes go to the unit immediately
// (SET_CONFIG, live); Save to unit makes them permanent (SAVE_CONFIG).

import EPDJ3000Kit
import EPDJ3000KitPure
import EPDJ3000Protocol
import SwiftUI

@MainActor
struct SettingsView: View {
    @EnvironmentObject var manager: DeviceManager
    @State private var selectedID = ""

    private var current: DeckSession? {
        manager.decks.first { $0.id == selectedID } ?? manager.decks.first
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            if let session = current {
                DeckPicker(sessions: manager.decks, selection: $selectedID)
                SettingsPane(session: session).id(session.id)
            } else {
                EmptyUnitView()
            }
        }
        .padding(16)
    }
}

/// Things that need a second click before they happen.
private enum UnitAction: Identifiable {
    case loadDefaults, swapDeck(to: Deck), reboot, bootloader

    var id: String {
        switch self {
        case .loadDefaults: return "defaults"
        case let .swapDeck(deck): return "swap\(deck.rawValue)"
        case .reboot: return "reboot"
        case .bootloader: return "bootloader"
        }
    }

    var title: String {
        switch self {
        case .loadDefaults: return "Load defaults? (the deck id stays)"
        case let .swapDeck(deck): return "Make this unit Deck \(deck == .a ? "A" : "B")?"
        case .reboot: return "Restart the unit?"
        case .bootloader: return "Enter the bootloader?"
        }
    }

    var message: String {
        switch self {
        case .loadDefaults:
            return "Replaces the live settings with the factory defaults, except the deck id: this unit keeps the deck it is. Nothing is permanent until you press Save to unit."
        case .swapDeck:
            return "The deck id is saved right away and the unit restarts, so it disappears for a moment. Its MIDI channel and USB name change with it."
        case .reboot:
            return "The unit disconnects and comes back in a few seconds."
        case .bootloader:
            return "The unit restarts into the ROM bootloader (firmware update mode) and stops answering until it is flashed or power-cycled."
        }
    }

    var confirmTitle: String {
        switch self {
        case .loadDefaults: return "Load defaults"
        case .swapDeck: return "Swap deck"
        case .reboot: return "Restart"
        case .bootloader: return "Enter bootloader"
        }
    }
}

@MainActor
struct SettingsPane: View {
    @ObservedObject var session: DeckSession
    @State private var pending: UnitAction?
    @State private var saved = false

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            NoticeBanner(text: $session.notice)
            actionBar
            if session.config.isEmpty {
                Text("No settings read yet.").foregroundStyle(.secondary)
            }
            Form {
                ForEach(ConfigPresentation.sections, id: \.self) { section in
                    Section(section) {
                        ForEach(ConfigKey.allCases.filter { ConfigPresentation.section(for: $0) == section }, id: \.self) { key in
                            ConfigRow(session: session, key: key)
                        }
                    }
                }
            }
            .formStyle(.grouped)
            .disabled(!session.isReady || session.config.isEmpty)
        }
        .alert(pending?.title ?? "", isPresented: Binding(get: { pending != nil }, set: { if !$0 { pending = nil } })) {
            // Captured here so the button still knows what it confirms whichever order SwiftUI clears `pending` in.
            let action = pending
            Button("Cancel", role: .cancel) { pending = nil }
            Button(action?.confirmTitle ?? "OK", role: .destructive) {
                if let action { run(action) }
                pending = nil
            }
        } message: {
            Text(pending?.message ?? "")
        }
    }

    private var actionBar: some View {
        HStack {
            Button("Save to unit") {
                Task { saved = await session.saveConfig() }
            }
            .buttonStyle(.borderedProminent)
            Button("Reload from unit") {
                saved = false
                Task { await session.reloadConfig() }
            }
            Button("Load defaults (keeps deck id)...") { pending = .loadDefaults }
            Spacer()
            Button(session.deck == .a ? "Make Deck B..." : "Make Deck A...") {
                pending = .swapDeck(to: session.deck == .a ? .b : .a)
            }
            Button("Restart...") { pending = .reboot }
            Button("Enter bootloader...") { pending = .bootloader }
        }
        .disabled(!session.isReady)
        .overlay(alignment: .bottomLeading) {
            if saved { Text("Saved.").font(.caption).foregroundStyle(.green).offset(y: 16) }
        }
    }

    private func run(_ action: UnitAction) {
        saved = false
        switch action {
        case .loadDefaults: Task { await session.loadDefaults() }
        case let .swapDeck(deck): Task { await session.setDeck(deck) }
        case .reboot: Task { await session.reboot(bootloader: false) }
        case .bootloader: Task { await session.reboot(bootloader: true) }
        }
    }
}

// MARK: - rows

@MainActor
struct ConfigRow: View {
    @ObservedObject var session: DeckSession
    let key: ConfigKey

    private var desc: ConfigDesc { ConfigTable.desc(key) }
    private var value: Int32? { session.config[key] }

    /// Greyed out (not hidden) when the unit's platter source does not use this setting.
    private var applicable: Bool {
        let source = CalibrationFlow.source(config: session.config[.platterSource], info: session.info?.platterSource)
        return ConfigPresentation.isApplicable(key, platterSource: source)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            control
            Text(caption)
                .font(.caption).foregroundStyle(.secondary)
        }
        .padding(.vertical, 2)
        .disabled(!applicable)
        .opacity(applicable ? 1 : 0.5)
    }

    private var caption: String {
        var text = desc.summary
        if ConfigPresentation.needsReboot(key) { text += " Takes effect after a restart." }
        if !applicable, let note = ConfigPresentation.inapplicableNote(for: key) { text += " " + note }
        return text
    }

    @ViewBuilder private var control: some View {
        if ConfigPresentation.isReadOnly(key) {
            // The unit refuses SET_CONFIG for these (the deck id changes through SET_DECK, which restarts the unit);
            // the action bar has that button.
            HStack {
                Text(desc.name)
                Spacer()
                Text(ConfigPresentation.label(for: key, value: value ?? desc.defaultValue))
                Text("(change it with Make Deck ... above)").font(.caption).foregroundStyle(.secondary)
            }
        } else {
            editor
        }
    }

    @ViewBuilder private var editor: some View {
        switch ConfigPresentation.kind(for: key) {
        case .toggle:
            Toggle(desc.name, isOn: Binding(
                get: { (value ?? desc.defaultValue) != 0 },
                set: { on in set(on ? 1 : 0) }))
        case let .choice(options):
            Picker(desc.name, selection: Binding(
                get: { value ?? desc.defaultValue },
                set: { set($0) })) {
                if let value, !options.contains(where: { $0.value == value }) {
                    Text("Unknown (\(value))").tag(value)
                }
                ForEach(options, id: \.value) { Text($0.label).tag($0.value) }
            }
        case let .bitfield(bits):
            HStack {
                Text(desc.name)
                Spacer()
                ForEach(bits, id: \.value) { bit in
                    Toggle(bit.label, isOn: Binding(
                        get: { (value ?? desc.defaultValue) & bit.value != 0 },
                        set: { on in
                            let current = value ?? desc.defaultValue
                            set(on ? current | bit.value : current & ~bit.value)
                        }))
                        .toggleStyle(.checkbox)
                }
            }
        case .integer:
            IntegerConfigRow(session: session, key: key)
        }
    }

    private func set(_ new: Int32) {
        Task { await session.setConfig(key, new) }
    }
}

/// A slider that only talks to the unit when released, plus the exact value.
@MainActor
struct IntegerConfigRow: View {
    @ObservedObject var session: DeckSession
    let key: ConfigKey
    @State private var draft = 0.0
    @State private var dragging = false

    private var desc: ConfigDesc { ConfigTable.desc(key) }

    var body: some View {
        HStack {
            Text(desc.name).frame(width: 190, alignment: .leading)
            Slider(value: $draft, in: Double(desc.minValue)...Double(desc.maxValue), step: 1,
                   onEditingChanged: { editing in
                       dragging = editing
                       if !editing { commit() }
                   })
            Text("\(Int(draft))").monospacedDigit().frame(width: 52, alignment: .trailing)
            Text(ConfigPresentation.unit(for: key) ?? "").font(.caption).foregroundStyle(.secondary).frame(width: 70, alignment: .leading)
        }
        .onAppear { draft = Double(session.config[key] ?? desc.defaultValue) }
        // Follow changes made elsewhere (Reload, Load defaults, calibration), but never fight a drag in progress.
        .onReceive(session.$config) { config in
            if !dragging { draft = Double(config[key] ?? desc.defaultValue) }
        }
    }

    private func commit() {
        let new = Int32(draft.rounded())
        guard new != session.config[key] else { return }
        Task {
            if !(await session.setConfig(key, new)) {
                draft = Double(session.config[key] ?? desc.defaultValue)   // the unit said no: show what it still has
            }
        }
    }
}

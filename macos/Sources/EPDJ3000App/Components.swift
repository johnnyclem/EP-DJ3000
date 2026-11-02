// Small shared views. Every view type in this target is @MainActor: SDKs before Xcode 16 only isolate `body`, and these
// views read main-actor session state from their helpers too.

import EPDJ3000Kit
import SwiftUI

/// Re-evaluates its content on a timer. For values that change faster than @Published should be asked to
/// (platter angle, meters, telemetry): the model keeps them in plain properties and this view polls them.
@MainActor
struct Live<Content: View>: View {
    private let start = Date()
    let hz: Double
    let content: (Date) -> Content

    init(hz: Double = 30, @ViewBuilder content: @escaping (Date) -> Content) {
        self.hz = hz
        self.content = content
    }

    var body: some View {
        // The closure is re-run on every tick; it does not read the context, so the tick time is simply "now".
        TimelineView(.periodic(from: start, by: 1 / hz)) { _ in
            content(Date())
        }
    }
}

@MainActor
struct Card<Content: View>: View {
    let title: String
    let content: Content

    init(_ title: String, @ViewBuilder content: () -> Content) {
        self.title = title
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title).font(.headline)
            content
        }
        .padding(14)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 10).fill(Color(nsColor: .controlBackgroundColor)))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color.gray.opacity(0.25), lineWidth: 1))
    }
}

@MainActor
struct LedDot: View {
    let on: Bool
    var color: Color = .orange

    var body: some View {
        Circle()
            .fill(on ? color : Color.gray.opacity(0.25))
            .frame(width: 12, height: 12)
            .overlay(Circle().stroke(Color.gray.opacity(0.4), lineWidth: 1))
    }
}

/// A labelled LED: `CUE`, `REV`, `TOUCH`...
@MainActor
struct LedLabel: View {
    let title: String
    let on: Bool
    var color: Color = .orange

    var body: some View {
        HStack(spacing: 6) {
            LedDot(on: on, color: color)
            Text(title).font(.caption)
        }
    }
}

/// A horizontal level bar for a value in 0...1.
@MainActor
struct MeterBar: View {
    let title: String
    let value: Double
    var detail: String = ""
    var tint: Color = .orange

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack {
                Text(title).font(.caption)
                Spacer()
                Text(detail).font(.caption).monospacedDigit().foregroundStyle(.secondary)
            }
            GeometryReader { geometry in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 3).fill(Color.gray.opacity(0.2))
                    RoundedRectangle(cornerRadius: 3).fill(tint)
                        .frame(width: max(0, min(1, value)) * geometry.size.width)
                }
            }
            .frame(height: 8)
        }
    }
}

@MainActor
struct KeyValueRow: View {
    let key: String
    let value: String

    init(_ key: String, _ value: String) {
        self.key = key
        self.value = value
    }

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            Text(key).foregroundStyle(.secondary)
            Spacer()
            Text(value).monospacedDigit().textSelection(.enabled)
        }
        .font(.callout)
    }
}

/// A dismissible message line.
@MainActor
struct NoticeBanner: View {
    @Binding var text: String?

    var body: some View {
        if let message = text {
            HStack(alignment: .top) {
                Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.orange)
                Text(message).font(.callout).frame(maxWidth: .infinity, alignment: .leading)
                Button("Dismiss") { text = nil }.buttonStyle(.borderless)
            }
            .padding(10)
            .background(RoundedRectangle(cornerRadius: 8).fill(Color.orange.opacity(0.15)))
        }
    }
}

/// Fires `onPress` when the pointer goes down and `onRelease` when it comes up: a momentary control (CUE, REV).
@MainActor
struct HoldButton: View {
    let title: String
    var systemImage: String?
    var lit = false
    let onPress: () -> Void
    let onRelease: () -> Void
    @State private var isDown = false

    var body: some View {
        HStack(spacing: 6) {
            if let systemImage { Image(systemName: systemImage) }
            Text(title).fontWeight(.semibold)
        }
        .frame(minWidth: 70)
        .padding(.vertical, 8)
        .padding(.horizontal, 14)
        .background(RoundedRectangle(cornerRadius: 8).fill((isDown || lit) ? Color.orange : Color.gray.opacity(0.25)))
        .foregroundStyle((isDown || lit) ? Color.black : Color.primary)
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { _ in
                    if !isDown {
                        isDown = true
                        onPress()
                    }
                }
                .onEnded { _ in
                    isDown = false
                    onRelease()
                }
        )
        .onDisappear {
            if isDown {
                isDown = false
                onRelease()
            }
        }
    }
}

/// Which unit a tab is about, when more than one is connected. A stored selection that no longer exists (unit
/// unplugged, or never chosen) shows as the first entry, which is also what the tabs fall back to.
@MainActor
struct DeckPicker: View {
    let sessions: [DeckSession]
    @Binding var selection: String

    var body: some View {
        if sessions.count > 1 {
            Picker("Deck", selection: Binding(
                get: { sessions.contains { $0.id == selection } ? selection : (sessions.first?.id ?? "") },
                set: { selection = $0 })) {
                ForEach(sessions) { session in
                    Text(session.label).tag(session.id)
                }
            }
            .pickerStyle(.segmented)
            .frame(maxWidth: 360)
        }
    }
}

@MainActor
struct EmptyUnitView: View {
    var message = "Connect an EP-DJ3000 over USB-C. It appears here by itself."

    var body: some View {
        VStack(spacing: 8) {
            Image(systemName: "cable.connector").font(.system(size: 36)).foregroundStyle(.secondary)
            Text("No unit connected").font(.title3)
            Text(message).foregroundStyle(.secondary).multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(40)
    }
}

enum Format {
    static func clock(_ seconds: Double) -> String {
        guard seconds.isFinite, seconds >= 0 else { return "0:00.0" }
        let whole = Int(seconds)
        let tenths = Int((seconds - Double(whole)) * 10)
        return "\(whole / 60):\(whole % 60 < 10 ? "0" : "")\(whole % 60).\(tenths)"
    }

    static func bits(_ value: UInt8, names: [String]) -> String {
        let on = names.enumerated().filter { value & (1 << UInt8($0.offset)) != 0 }.map(\.element)
        return on.isEmpty ? "-" : on.joined(separator: " ")
    }
}

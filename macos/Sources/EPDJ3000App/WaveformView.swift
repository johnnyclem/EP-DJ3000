// Whole-track waveform with the playhead and cue point drawn over it. Click or drag to seek.

import EPDJ3000Engine
import EPDJ3000Kit
import EPDJ3000KitPure
import SwiftUI

@MainActor
struct WaveformView: View {
    let overview: WaveformOverview
    let player: ScratchPlayerCore

    var body: some View {
        GeometryReader { geometry in
            ZStack {
                RoundedRectangle(cornerRadius: 6).fill(Color(nsColor: .textBackgroundColor))
                // The waveform itself never changes while a track is loaded, so it sits outside the timeline.
                WaveformShape(overview: overview)
                Live(hz: 30) { _ in
                    PlayheadOverlay(status: player.status)
                }
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0).onChanged { value in
                    let status = player.status
                    guard status.hasTrack, geometry.size.width > 0 else { return }
                    let fraction = max(0, min(1, Double(value.location.x / geometry.size.width)))
                    player.send(.seek(seconds: fraction * status.durationSeconds))
                }
            )
        }
        .frame(height: 110)
    }
}

@MainActor
struct WaveformShape: View {
    let overview: WaveformOverview

    var body: some View {
        Canvas { context, size in
            let peaks = overview.peaks
            guard !peaks.isEmpty, size.width >= 1 else { return }
            let columns = Int(size.width)
            let middle = size.height / 2
            var path = Path()
            for x in 0..<columns {
                // The largest peak among the buckets this pixel column covers.
                let from = x * peaks.count / columns
                let to = max(from + 1, (x + 1) * peaks.count / columns)
                var peak: Float = 0
                for i in from..<min(to, peaks.count) { peak = max(peak, peaks[i]) }
                let half = CGFloat(peak) * middle * 0.95
                path.addRect(CGRect(x: CGFloat(x), y: middle - half, width: 1, height: max(1, half * 2)))
            }
            context.fill(path, with: .color(Color.orange.opacity(0.8)))
        }
    }
}

@MainActor
struct PlayheadOverlay: View {
    let status: PlayerStatus

    var body: some View {
        GeometryReader { geometry in
            if status.hasTrack {
                let width = geometry.size.width
                let playhead = CGFloat(status.playheadFrames / Double(max(1, status.trackFrames - 1))) * width
                let cue = CGFloat(status.cueFrames / Double(max(1, status.trackFrames - 1))) * width
                Rectangle().fill(Color.blue.opacity(0.9)).frame(width: 2).offset(x: cue - 1)
                Rectangle().fill(Color.white).frame(width: 2).offset(x: playhead - 1)
            }
        }
        .allowsHitTesting(false)
    }
}

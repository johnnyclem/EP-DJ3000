// One deck's sound: the ScratchPlayerCore, the track loaded into it, and the audio output it plays through.
// The engine is started when something is loaded and rebuilt whenever the output device changes.

import Combine
import EPDJ3000Engine
import EPDJ3000KitPure
import Foundation

@MainActor
public final class DeckAudio: ObservableObject {
    public static let sampleRate = TestSignal.defaultSampleRate

    public enum Output: Hashable, Sendable {
        case systemDefault
        case device(uid: String)
    }

    public struct TrackInfo: Equatable, Sendable {
        public let name: String
        public let source: String
        public let duration: Double
        public let overview: WaveformOverview
    }

    public let player = ScratchPlayerCore(sampleRate: DeckAudio.sampleRate)

    @Published public private(set) var track: TrackInfo?
    @Published public private(set) var isLoading = false
    /// Last load or output problem, for the UI.
    @Published public private(set) var message: String?
    @Published public private(set) var output: Output = .systemDefault
    @Published public private(set) var isEngineRunning = false

    private var engine: AudioOutput?
    private var loadTask: Task<Void, Never>?
    private var loadGeneration = 0
    private var lastRebuild = 0.0
    private var pendingRestart: Task<Void, Never>?
    private static let minRebuildInterval = 0.5

    public init() {}

    // MARK: loading

    public func load(url: URL) {
        startLoading(thenPlay: false) { try await TrackLoader.load(url: url) }
    }

    public enum Signal: Sendable {
        case demoLoop
        case sine440
    }

    /// A generated signal instead of a file: needs no DAC and no decoder.
    public func loadSignal(_ signal: Signal, thenPlay: Bool = false) {
        startLoading(thenPlay: thenPlay) {
            await Task.detached(priority: .userInitiated) { () -> LoadedTrack in
                switch signal {
                case .demoLoop:
                    let s = TestSignal.demoLoop()
                    return TrackLoader.wrap(name: "Demo loop (120 BPM)", source: "generated", left: s.left, right: s.right)
                case .sine440:
                    let s = TestSignal.sine(frequency: 440, seconds: 20)
                    return TrackLoader.wrap(name: "440 Hz test tone", source: "generated", left: s.left, right: s.right)
                }
            }.value
        }
    }

    private func startLoading(thenPlay: Bool, _ work: @escaping @Sendable () async throws -> LoadedTrack) {
        loadTask?.cancel()
        loadGeneration += 1
        let generation = loadGeneration
        isLoading = true
        message = nil
        loadTask = Task { [weak self] in
            do {
                let loaded = try await work()
                guard let self, generation == self.loadGeneration, !Task.isCancelled else { return }
                self.finishLoading(loaded, thenPlay: thenPlay)
            } catch {
                guard let self, generation == self.loadGeneration else { return }
                self.isLoading = false
                self.message = ErrorText.describe(error)
            }
        }
    }

    private func finishLoading(_ loaded: LoadedTrack, thenPlay: Bool) {
        player.load(loaded.buffer)
        track = TrackInfo(name: loaded.name, source: loaded.source, duration: loaded.buffer.duration, overview: loaded.overview)
        isLoading = false
        ensureEngine()
        // Queued behind the load: the render thread picks up the track first, then runs the command.
        if thenPlay { player.send(.play) }
    }

    public func unload() {
        loadTask?.cancel()
        loadGeneration += 1
        isLoading = false
        player.load(nil)
        track = nil
    }

    // MARK: output

    public func setOutput(_ new: Output) {
        output = new
        message = nil
        if engine != nil || track != nil { rebuildEngine() }
    }

    /// Starts the engine if it is not running; harmless otherwise.
    public func ensureEngine() {
        if let engine, engine.isRunning { return }
        rebuildEngine()
    }

    public func shutdown() {
        loadTask?.cancel()
        pendingRestart?.cancel()
        pendingRestart = nil
        engine?.stop()
        engine = nil
        isEngineRunning = false
    }

    private func rebuildEngine() {
        // A pending restart was timed from the previous rebuild; this one starts a new interval.
        pendingRestart?.cancel()
        pendingRestart = nil
        engine?.stop()
        engine = nil
        lastRebuild = HostClock.now()

        var deviceID: UInt32?
        if case let .device(uid) = output {
            deviceID = AudioDevices.outputDevices().first { $0.uid == uid }?.deviceID
            if deviceID == nil { message = "The chosen output is not connected; playing through the system default instead." }
        }
        do {
            let new = try AudioOutput(player: player, deviceID: deviceID) { [weak self] in
                Task { @MainActor in self?.engineConfigurationChanged() }
            }
            try new.start()
            engine = new
            isEngineRunning = true
        } catch {
            isEngineRunning = false
            message = "Audio output failed: \(ErrorText.describe(error))"
        }
    }

    private func engineConfigurationChanged() {
        // The hardware changed under a running engine (unplugged device, sample-rate switch). AVAudioEngine has
        // stopped itself by the time it posts this, so every notification must end in a restart: dropping one leaves
        // the deck silent. Rebuilds are at least minRebuildInterval apart so a flapping device cannot spin; a
        // notification that arrives sooner (typically the replacement engine stopping too) schedules one restart for
        // when the interval is up. That restart is a no-op if the engine is running by then (a stale notification).
        let wait = Self.minRebuildInterval - (HostClock.now() - lastRebuild)
        guard wait > 0 else {
            rebuildEngine()
            return
        }
        guard pendingRestart == nil else { return }
        pendingRestart = Task { [weak self] in
            try? await Task.sleep(nanoseconds: UInt64(wait * 1e9))
            guard !Task.isCancelled, let self else { return }
            self.pendingRestart = nil
            self.ensureEngine()
        }
    }
}

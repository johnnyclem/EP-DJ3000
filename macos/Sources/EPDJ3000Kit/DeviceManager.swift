// The app's view of the hardware: which units are plugged in, one DeckSession each, hot-plug handling, and the
// list of audio outputs. Units are matched by name (EP-DJ3000) and told apart by GET_INFO, not by the name.

import Combine
import EPDJ3000Protocol
import Foundation

@MainActor
public final class DeviceManager: ObservableObject {
    /// Connected units, in discovery order. Use `decks` for display order.
    @Published public private(set) var sessions: [DeckSession] = []
    @Published public private(set) var audioDevices: [AudioOutputDevice] = []
    /// Set when CoreMIDI itself could not be started.
    @Published public private(set) var midiError: String?

    public let log: MidiLog
    /// Always present: a player with no unit behind it, so playback can be tried without hardware.
    public let virtualDeck: DeckSession

    private struct Entry {
        let session: DeckSession
        let cable: MidiCable?
        let observation: AnyCancellable
    }

    private var service: CoreMidiService?
    private var entries: [Int32: Entry] = [:]
    private var rescanTask: Task<Void, Never>?
    private var started = false

    public init() {
        let log = MidiLog()
        self.log = log
        self.virtualDeck = DeckSession(virtualWith: log)
    }

    /// Creates the CoreMIDI client, so call it on the main thread. Safe to call twice.
    public func start() {
        guard !started else { return }
        started = true
        do {
            let service = try CoreMidiService()
            service.setChangeHandler { [weak self] in
                Task { @MainActor in self?.scheduleRescan() }
            }
            self.service = service
        } catch {
            midiError = ErrorText.describe(error)
        }
        rescan()
    }

    /// Stops every unit session and the audio engines.
    public func shutdown() {
        rescanTask?.cancel()
        service?.setChangeHandler(nil)
        for entry in entries.values { entry.session.shutdown() }
        entries.removeAll()
        sessions = []
        virtualDeck.shutdown()
    }

    // MARK: display helpers

    /// Units in Deck A, Deck B order (a deck whose id is not yet confirmed sorts by its name's hint).
    public var decks: [DeckSession] {
        sessions.sorted { ($0.deck.rawValue, $0.id) < ($1.deck.rawValue, $1.id) }
    }

    public func session(for deck: Deck) -> DeckSession? {
        decks.first { $0.deck == deck }
    }

    /// Everything the Player tab can drive: the units, then the on-screen deck.
    public var players: [DeckSession] { decks + [virtualDeck] }

    /// Decks claimed by more than one confirmed unit.
    public var conflictingDecks: Set<Deck> {
        var seen = Set<Deck>()
        var duplicates = Set<Deck>()
        for session in sessions where session.isDeckConfirmed {
            if !seen.insert(session.deck).inserted { duplicates.insert(session.deck) }
        }
        return duplicates
    }

    /// The CoreAudio output that belongs to a deck's USB audio function, if macOS lists one.
    public func audioDevice(for deck: Deck) -> AudioOutputDevice? {
        audioDevices.first { DeckNameParser.audioDevice(named: $0.name, isDeck: deck) }
    }

    public func refreshAudioDevices() {
        audioDevices = AudioDevices.outputDevices()
    }

    // MARK: discovery

    private func scheduleRescan() {
        // CoreMIDI reports a plug-in as a burst, sometimes before the endpoints exist: wait for it to settle.
        rescanTask?.cancel()
        rescanTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 200_000_000)
            guard !Task.isCancelled else { return }
            self?.rescan()
        }
    }

    public func rescan() {
        refreshAudioDevices()
        guard let service else { return }
        midiError = nil   // a unit that failed to open half-enumerated is retried here; the error returns if it still fails
        let units = service.discoverUnits()
        let present = Set(units.map(\.deviceUniqueID))

        for uid in Array(entries.keys) where !present.contains(uid) {
            entries[uid]?.session.shutdown()
            entries[uid] = nil
        }
        for unit in units {
            let cable = unit.cable(Int(Proto.Cable.deck))
            if let existing = entries[unit.deviceUniqueID] {
                if existing.cable == cable { continue }
                // Replugged into another port: the endpoint refs changed, so start over.
                existing.session.shutdown()
                entries[unit.deviceUniqueID] = nil
            }
            do {
                let session = try DeckSession(unit: unit, service: service, log: log)
                let observation = session.objectWillChange.sink { [weak self] _ in self?.objectWillChange.send() }
                entries[unit.deviceUniqueID] = Entry(session: session, cable: cable, observation: observation)
                session.start()
            } catch {
                midiError = "\(unit.deviceName): \(ErrorText.describe(error))"
            }
        }
        sessions = entries.values.map(\.session).sorted { $0.id < $1.id }
    }
}

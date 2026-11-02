// Thread-safe ring of MIDI traffic for the Test tab. Appended to from CoreMIDI's thread, the main actor and
// DeviceClient's executor; read by the UI a few times a second. Deliberately not an ObservableObject: at 500 Hz
// jog it would invalidate the view tree on every message, so views poll `snapshot` on a timeline instead.

import EPDJ3000Protocol
import Foundation

public final class MidiLog: @unchecked Sendable {
    public enum Direction: String, Sendable {
        case rx = "RX"
        case tx = "TX"
    }

    public struct Entry: Identifiable, Sendable {
        public let id: Int
        /// `HostClock` seconds.
        public let time: Double
        /// A `DeckSession.id`; resolve to a display name with `name(forSource:)`.
        public let source: String
        /// Where on the unit it came from or went to: "" for the deck cable, "TRS" or "Link" for the other cables.
        public let cable: String
        public let direction: Direction
        public let event: MidiEvent
        /// Consecutive identical clock ticks are folded into one entry; this is how many.
        public var repeatCount: Int
    }

    public static let capacity = 20_000

    private let lock = NSLock()
    private var entries: [Entry] = []
    private var names: [String: String] = [:]
    private var nextId = 0
    private var paused = false

    public init() {
        entries.reserveCapacity(Self.capacity + 4_000)
    }

    public var isPaused: Bool {
        get { lock.lock(); defer { lock.unlock() }; return paused }
        set { lock.lock(); paused = newValue; lock.unlock() }
    }

    public func setName(_ name: String, forSource source: String) {
        lock.lock()
        names[source] = name
        lock.unlock()
    }

    public func name(forSource source: String) -> String {
        lock.lock()
        defer { lock.unlock() }
        return names[source] ?? source
    }

    public func append(source: String, cable: String = "", direction: Direction, event: MidiEvent, time: Double) {
        lock.lock()
        defer { lock.unlock() }
        guard !paused else { return }
        if case .clock = event, var last = entries.last, case .clock = last.event,
           last.source == source, last.cable == cable, last.direction == direction {
            last.repeatCount += 1
            entries[entries.count - 1] = last
            return
        }
        entries.append(Entry(id: nextId, time: time, source: source, cable: cable, direction: direction, event: event, repeatCount: 1))
        nextId += 1
        if entries.count > Self.capacity + 4_000 { entries.removeFirst(entries.count - Self.capacity) }
    }

    /// Parses `bytes` (outgoing traffic is handed over as raw bytes) and appends each message.
    public func append(source: String, cable: String = "", direction: Direction, bytes: [UInt8], time: Double) {
        var parser = MidiStreamParser()
        for event in parser.feed(bytes) {
            append(source: source, cable: cable, direction: direction, event: event, time: time)
        }
    }

    public func clear() {
        lock.lock()
        entries.removeAll(keepingCapacity: true)
        lock.unlock()
    }

    public var count: Int {
        lock.lock()
        defer { lock.unlock() }
        return entries.count
    }

    /// The newest `limit` entries accepted by `filter`, oldest first. `filter` runs under the log's lock, so it must
    /// not call back into the log.
    public func recent(limit: Int, where filter: (Entry) -> Bool = { _ in true }) -> [Entry] {
        lock.lock()
        defer { lock.unlock() }
        var out = [Entry]()
        out.reserveCapacity(limit)
        for entry in entries.reversed() where filter(entry) {
            out.append(entry)
            if out.count == limit { break }
        }
        return out.reversed()
    }
}

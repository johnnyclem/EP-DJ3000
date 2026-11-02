// CoreMIDI client: hot-plug notifications and discovery of EP-DJ3000 units.
//
// Create it on the main thread: CoreMIDI delivers setup-change notifications on the run loop of the thread that
// created the client.

import CoreMIDI
import EPDJ3000Protocol
import Foundation

public enum CoreMidiError: LocalizedError, CustomStringConvertible {
    case osStatus(String, OSStatus)
    case noDestination
    case bufferTooSmall

    public var description: String {
        switch self {
        case let .osStatus(call, status): return "\(call) failed (OSStatus \(status))"
        case .noDestination: return "This unit has no MIDI input endpoint to send to."
        case .bufferTooSmall: return "MIDI message too large to send."
        }
    }

    public var errorDescription: String? { description }
}

/// One USB-MIDI cable of a unit (docs/PROTOCOL.md section 1). Endpoint refs change when the device is replugged.
public struct MidiCable: Equatable, Sendable {
    public var index: Int
    /// Device -> app.
    public var source: MIDIEndpointRef?
    /// App -> device.
    public var destination: MIDIEndpointRef?
    public var name: String

    /// "" for the deck cable itself.
    public var label: String {
        switch index {
        case Int(Proto.Cable.deck): return ""
        case Int(Proto.Cable.trsMidi): return "TRS"
        case Int(Proto.Cable.linkedDeck): return "Link"
        default: return "cable \(index)"
        }
    }
}

/// An EP-DJ3000 as CoreMIDI lists it.
public struct UnitEndpoints: Equatable, Sendable, Identifiable {
    public var deviceUniqueID: Int32
    public var deviceName: String
    /// From the name only; GET_INFO has the final word.
    public var deckHint: Deck?
    public var cables: [MidiCable]

    public var id: Int32 { deviceUniqueID }

    public func cable(_ index: Int) -> MidiCable? {
        cables.first { $0.index == index }
    }
}

public final class CoreMidiService: @unchecked Sendable {
    private var client = MIDIClientRef()
    private var outPort = MIDIPortRef()
    private let lock = NSLock()
    private var changeHandler: (@Sendable () -> Void)?

    public init() throws {
        var newClient = MIDIClientRef()
        var status = MIDIClientCreateWithBlock("EP-DJ3000 Test App" as CFString, &newClient) { [weak self] _ in
            // Any change at all (devices, entities, endpoints, properties): the handler re-reads everything and diffs.
            self?.notifyChanged()
        }
        guard status == noErr else { throw CoreMidiError.osStatus("MIDIClientCreateWithBlock", status) }
        var newPort = MIDIPortRef()
        status = MIDIOutputPortCreate(newClient, "EP-DJ3000 out" as CFString, &newPort)
        guard status == noErr else {
            MIDIClientDispose(newClient)
            throw CoreMidiError.osStatus("MIDIOutputPortCreate", status)
        }
        client = newClient
        outPort = newPort
    }

    deinit {
        MIDIClientDispose(client)
    }

    var clientRef: MIDIClientRef { client }
    var outputPortRef: MIDIPortRef { outPort }

    /// Called (on the thread that created the service) whenever CoreMIDI says something changed. Fires in bursts.
    public func setChangeHandler(_ handler: (@Sendable () -> Void)?) {
        lock.lock()
        changeHandler = handler
        lock.unlock()
    }

    private func notifyChanged() {
        lock.lock()
        let handler = changeHandler
        lock.unlock()
        handler?()
    }

    // MARK: discovery

    /// Every online EP-DJ3000 device with its endpoints. A device that is remembered but unplugged is "offline" and skipped.
    public func discoverUnits() -> [UnitEndpoints] {
        var units = [UnitEndpoints]()
        for d in 0..<MIDIGetNumberOfDevices() {
            let device = MIDIGetDevice(d)
            guard device != 0 else { continue }
            let name = Self.string(device, kMIDIPropertyName) ?? ""
            let model = Self.string(device, kMIDIPropertyModel) ?? ""
            guard DeckNameParser.isEPDJ3000(name) || DeckNameParser.isEPDJ3000(model) else { continue }
            if (Self.integer(device, kMIDIPropertyOffline) ?? 0) != 0 { continue }

            // Endpoints in entity order. The USB-MIDI driver creates one source and one destination per cable, in cable
            // order, so the n-th endpoint is cable n. (Not checked against hardware: see README.)
            var sources = [MIDIEndpointRef]()
            var destinations = [MIDIEndpointRef]()
            for e in 0..<MIDIDeviceGetNumberOfEntities(device) {
                let entity = MIDIDeviceGetEntity(device, e)
                for s in 0..<MIDIEntityGetNumberOfSources(entity) { sources.append(MIDIEntityGetSource(entity, s)) }
                for t in 0..<MIDIEntityGetNumberOfDestinations(entity) { destinations.append(MIDIEntityGetDestination(entity, t)) }
            }
            var cables = [MidiCable]()
            for i in 0..<max(sources.count, destinations.count) {
                let source = i < sources.count ? sources[i] : nil
                let destination = i < destinations.count ? destinations[i] : nil
                let endpointName = source.flatMap { Self.string($0, kMIDIPropertyName) }
                    ?? destination.flatMap { Self.string($0, kMIDIPropertyName) } ?? name
                cables.append(MidiCable(index: i, source: source, destination: destination, name: endpointName))
            }
            let uniqueID = Self.integer(device, kMIDIPropertyUniqueID) ?? Int32(truncatingIfNeeded: device)
            units.append(UnitEndpoints(deviceUniqueID: uniqueID, deviceName: name,
                                       deckHint: DeckNameParser.deck(fromName: name), cables: cables))
        }
        return units.sorted { ($0.deckHint?.rawValue ?? 9, $0.deviceUniqueID) < ($1.deckHint?.rawValue ?? 9, $1.deviceUniqueID) }
    }

    // MARK: opening

    func openTransport(_ cable: MidiCable) throws -> CoreMidiTransport {
        try CoreMidiTransport(service: self, cable: cable)
    }

    /// Listen-only connection to one cable, for the monitor.
    func openInput(_ cable: MidiCable, handler: @escaping @Sendable (TimedMidi) -> Void) throws -> CoreMidiInput? {
        guard let source = cable.source else { return nil }
        return try CoreMidiInput(client: client, source: source, name: "EP-DJ3000 in \(cable.index)", handler: handler)
    }

    // MARK: properties

    private static func string(_ object: MIDIObjectRef, _ property: CFString) -> String? {
        var value: Unmanaged<CFString>?
        guard MIDIObjectGetStringProperty(object, property, &value) == noErr, let value else { return nil }
        return value.takeRetainedValue() as String
    }

    private static func integer(_ object: MIDIObjectRef, _ property: CFString) -> Int32? {
        var value: Int32 = 0
        guard MIDIObjectGetIntegerProperty(object, property, &value) == noErr else { return nil }
        return value
    }
}

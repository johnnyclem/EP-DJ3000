// MidiTransport over one USB-MIDI cable (the "Deck" cable) of one unit, so DeviceClient works unchanged.

import CoreMIDI
import EPDJ3000Protocol
import Foundation

public final class CoreMidiTransport: MidiTransport, @unchecked Sendable {
    /// Raw bytes for `DeviceClient` (single consumer).
    public let incoming: AsyncStream<[UInt8]>
    /// The same bytes with the CoreMIDI packet timestamps, for the deck model and the monitor (single consumer).
    public let timedInput: AsyncStream<TimedMidi>

    private let incomingContinuation: AsyncStream<[UInt8]>.Continuation
    private let timedContinuation: AsyncStream<TimedMidi>.Continuation
    private let outputPort: MIDIPortRef
    private let destination: MIDIEndpointRef?
    private let lock = NSLock()
    private var input: CoreMidiInput?
    private var txObserver: (@Sendable (Double, [UInt8]) -> Void)?

    init(service: CoreMidiService, cable: MidiCable) throws {
        var rawContinuation: AsyncStream<[UInt8]>.Continuation!
        incoming = AsyncStream { rawContinuation = $0 }
        var timedContinuationBox: AsyncStream<TimedMidi>.Continuation!
        timedInput = AsyncStream { timedContinuationBox = $0 }
        let rawOut = rawContinuation!
        let timedOut = timedContinuationBox!
        incomingContinuation = rawOut
        timedContinuation = timedOut
        outputPort = service.outputPortRef
        destination = cable.destination
        input = try service.openInput(cable) { chunk in
            rawOut.yield(chunk.bytes)
            timedOut.yield(chunk)
        }
    }

    deinit {
        close()
    }

    /// Called with the host time and the bytes of every successful `send` (any thread), for the SysEx log.
    public func setTxObserver(_ observer: (@Sendable (Double, [UInt8]) -> Void)?) {
        lock.lock()
        txObserver = observer
        lock.unlock()
    }

    /// Stops listening and ends both streams, which makes `DeviceClient` close itself. Idempotent.
    public func close() {
        lock.lock()
        let toClose = input
        input = nil
        txObserver = nil
        lock.unlock()
        toClose?.close()
        incomingContinuation.finish()
        timedContinuation.finish()
    }

    // MARK: MidiTransport

    public func send(_ bytes: [UInt8]) async throws {
        guard !bytes.isEmpty else { return }
        guard let destination else { throw CoreMidiError.noDestination }
        try Self.write(bytes, port: outputPort, to: destination)
        notifyTransmitted(bytes)
    }

    // Synchronous on purpose: NSLock must not be taken directly in an async function.
    private func notifyTransmitted(_ bytes: [UInt8]) {
        lock.lock()
        let observer = txObserver
        lock.unlock()
        observer?(HostClock.now(), bytes)
    }

    /// One packet, timestamp 0 (= now). A SysEx goes out whole in one packet; CoreMIDI splits it for USB.
    private static func write(_ bytes: [UInt8], port: MIDIPortRef, to destination: MIDIEndpointRef) throws {
        let size = 256 + bytes.count
        let raw = UnsafeMutableRawPointer.allocate(byteCount: size, alignment: 8)
        defer { raw.deallocate() }
        let list = raw.bindMemory(to: MIDIPacketList.self, capacity: 1)
        let first = MIDIPacketListInit(list)
        // `MIDIPacketListAdd` takes `ByteCount`s and may return an optional; `byteCount(_:)` and the optional-typed constant
        // keep this compiling whichever way the SDK imports them.
        let added: UnsafeMutablePointer<MIDIPacket>? = bytes.withUnsafeBufferPointer { buffer in
            MIDIPacketListAdd(list, byteCount(size), first, 0, byteCount(buffer.count), buffer.baseAddress!)
        }
        guard added != nil else { throw CoreMidiError.bufferTooSmall }
        let status = MIDISend(port, destination, list)
        guard status == noErr else { throw CoreMidiError.osStatus("MIDISend", status) }
    }
}

/// Converts to whatever integer type the callee wants (`Int` or `UInt`).
private func byteCount<T: BinaryInteger>(_ n: Int) -> T { T(n) }

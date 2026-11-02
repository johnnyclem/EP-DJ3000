// One input port connected to one source endpoint. The handler runs on CoreMIDI's own thread, once per packet,
// serially for a given port.

import CoreMIDI
import Foundation

/// A chunk of MIDI bytes as it arrived: one CoreMIDI packet. A packet may hold several messages, or just part of a SysEx.
public struct TimedMidi: Sendable {
    /// `HostClock` seconds: the packet's own timestamp when CoreMIDI gave one, else the time we saw it.
    public var time: Double
    public var bytes: [UInt8]

    public init(time: Double, bytes: [UInt8]) {
        self.time = time
        self.bytes = bytes
    }
}

public final class CoreMidiInput: @unchecked Sendable {
    // MIDIPacketList is `UInt32 numPackets; MIDIPacket packet[1]` and MIDIPacket is `UInt64 timeStamp; UInt16 length;
    // Byte data[256]`, both packed to 4. The fallbacks are those documented offsets for the case the key path is nil.
    private static let firstPacketOffset = MemoryLayout<MIDIPacketList>.offset(of: \MIDIPacketList.packet) ?? 4
    private static let dataOffset = MemoryLayout<MIDIPacket>.offset(of: \MIDIPacket.data) ?? 10

    private let lock = NSLock()
    private var port = MIDIPortRef()
    private var source = MIDIEndpointRef()

    init(client: MIDIClientRef, source: MIDIEndpointRef, name: String,
         handler: @escaping @Sendable (TimedMidi) -> Void) throws {
        var created = MIDIPortRef()
        var status = MIDIInputPortCreateWithBlock(client, name as CFString, &created) { list, _ in
            CoreMidiInput.deliver(list, to: handler)
        }
        guard status == noErr else { throw CoreMidiError.osStatus("MIDIInputPortCreateWithBlock", status) }
        status = MIDIPortConnectSource(created, source, nil)
        guard status == noErr else {
            MIDIPortDispose(created)
            throw CoreMidiError.osStatus("MIDIPortConnectSource", status)
        }
        self.port = created
        self.source = source
    }

    deinit {
        close()
    }

    /// Safe to call more than once, and after the device has gone.
    public func close() {
        lock.lock()
        let (p, s) = (port, source)
        port = 0
        source = 0
        lock.unlock()
        guard p != 0 else { return }
        MIDIPortDisconnectSource(p, s)
        MIDIPortDispose(p)
    }

    /// Walks the packet list in place with `MIDIPacketNext`, which knows the alignment rules of the CPU
    /// (packets are 4-byte aligned on ARM, contiguous on Intel). Copying the first packet out of the list
    /// and advancing from the copy would read stack garbage for every packet after the first.
    static func deliver(_ list: UnsafePointer<MIDIPacketList>, to handler: (TimedMidi) -> Void) {
        let count = Int(list.pointee.numPackets)
        guard count > 0 else { return }
        var packet = UnsafeRawPointer(list).advanced(by: firstPacketOffset).assumingMemoryBound(to: MIDIPacket.self)
        for _ in 0..<count {
            let length = Int(packet.pointee.length)
            if length > 0 {
                let ticks = packet.pointee.timeStamp
                let data = UnsafeRawPointer(packet).advanced(by: dataOffset).assumingMemoryBound(to: UInt8.self)
                handler(TimedMidi(time: ticks == 0 ? HostClock.now() : HostClock.seconds(fromHostTicks: ticks),
                                  bytes: Array(UnsafeBufferPointer(start: data, count: length))))
            }
            packet = UnsafePointer(MIDIPacketNext(packet))
        }
    }
}

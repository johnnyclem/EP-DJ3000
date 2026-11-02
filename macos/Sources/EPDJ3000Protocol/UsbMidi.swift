// USB-MIDI 1.0 event packets: 4 bytes = (cable << 4 | CIN), then up to 3 MIDI bytes.
// CoreMIDI hides this on macOS; it exists for raw-USB hosts, tools and conformance with the firmware.

public struct UsbMidiPacket: Equatable, Sendable {
    public var cable: UInt8
    public var cin: UInt8
    /// Only the MIDI bytes the CIN says are valid (0...3).
    public var bytes: [UInt8]

    public init(cable: UInt8, cin: UInt8, bytes: [UInt8]) {
        self.cable = cable
        self.cin = cin
        self.bytes = bytes
    }
}

public enum UsbMidi {
    /// Number of valid MIDI bytes for a Code Index Number. 0 for the reserved CINs 0x0 and 0x1,
    /// which also covers all-zero padding packets.
    public static func byteCount(cin: UInt8) -> Int {
        switch cin & 0x0F {
        case 0x0, 0x1: return 0
        case 0x2, 0x6, 0xC, 0xD: return 2
        case 0x5, 0xF: return 1
        default: return 3  // 0x3, 0x4, 0x7, 0x8...0xB, 0xE
        }
    }

    /// Splits flat USB data into packets. Reserved/padding packets are dropped; a trailing partial packet is ignored.
    public static func decode(_ data: [UInt8]) -> [UsbMidiPacket] {
        var out = [UsbMidiPacket]()
        var i = 0
        while i + 4 <= data.count {
            let cin = data[i] & 0x0F
            let n = byteCount(cin: cin)
            if n > 0 {
                out.append(UsbMidiPacket(cable: data[i] >> 4, cin: cin, bytes: Array(data[(i + 1)...(i + n)])))
            }
            i += 4
        }
        return out
    }

    /// Wraps ONE complete MIDI message (channel, system common, real-time, or `F0 ... F7`) into flat
    /// 4-byte packets. Returns nil for anything that is not exactly one well-formed message or a cable > 15.
    public static func encode(cable: UInt8, message: [UInt8]) -> [UInt8]? {
        guard cable < 16, let status = message.first, status >= 0x80 else { return nil }

        if status == 0xF0 {
            guard message.count >= 2, message.last == 0xF7,
                  message[1..<(message.count - 1)].allSatisfy({ $0 < 0x80 }) else { return nil }
            var out = [UInt8]()
            out.reserveCapacity((message.count + 2) / 3 * 4)
            var i = 0
            while i < message.count {
                let n = min(3, message.count - i)
                let last = i + n == message.count
                let cin: UInt8 = last ? (n == 1 ? 0x5 : n == 2 ? 0x6 : 0x7) : 0x4
                out.append(cable << 4 | cin)
                for j in 0..<3 { out.append(j < n ? message[i + j] : 0) }
                i += n
            }
            return out
        }

        let cin: UInt8
        let n: Int
        switch status {
        case 0xF8...0xFF: (cin, n) = (0xF, 1)
        case 0xF1, 0xF3: (cin, n) = (0x2, 2)
        case 0xF2: (cin, n) = (0x3, 3)
        case 0xF4...0xF7: (cin, n) = (0x5, 1)
        default: (cin, n) = (status >> 4, (status >> 4 == 0xC || status >> 4 == 0xD) ? 2 : 3)
        }
        guard message.count == n, message.dropFirst().allSatisfy({ $0 < 0x80 }) else { return nil }
        return [cable << 4 | cin] + message + [UInt8](repeating: 0, count: 3 - n)
    }
}

// SysEx framing for the EP-DJ3000 configuration protocol (docs/PROTOCOL.md section 3).
//
//   F0 7D 45 44 <ver> <deck> <cmd> <packed payload ...> <checksum> F7

/// Why a byte sequence is not an acceptable EP-DJ3000 SysEx frame. Distinct from `SysExError`,
/// which is the error code a device reports in a NAK.
public enum SysExFrameError: Error, Equatable, Sendable {
    case missingFraming
    /// Well-formed SysEx, but not ours (other manufacturer or model id). Not an error for a MIDI monitor.
    case notEPDJ3000
    case tooShort
    case unsupportedVersion(UInt8)
    /// A byte >= 0x80 between F0 and F7.
    case dataByteOutOfRange
    case badChecksum
    /// Payload septets are not a valid 8->7 stream.
    case badPacking
}

public enum SysExCodec {
    public static let header: [UInt8] = [Proto.SysEx.manufacturerId, Proto.SysEx.modelId0, Proto.SysEx.modelId1]

    /// Groups of up to 7 bytes; each group is a header septet (bit i = bit 7 of byte i) then the 7 low bits of each byte.
    public static func pack8to7(_ raw: [UInt8]) -> [UInt8] {
        var out = [UInt8]()
        out.reserveCapacity(raw.count + (raw.count + 6) / 7)
        var i = 0
        while i < raw.count {
            let n = min(7, raw.count - i)
            var hdr: UInt8 = 0
            for j in 0..<n { hdr |= (raw[i + j] >> 7) << UInt8(j) }
            out.append(hdr)
            for j in 0..<n { out.append(raw[i + j] & 0x7F) }
            i += n
        }
        return out
    }

    /// Inverse of `pack8to7`. Returns nil for anything `pack8to7` cannot have produced:
    /// a byte >= 0x80, or a header septet with no data after it.
    public static func unpack7to8(_ septets: [UInt8]) -> [UInt8]? {
        var out = [UInt8]()
        out.reserveCapacity(septets.count)
        var i = 0
        while i < septets.count {
            let hdr = septets[i]
            let n = min(7, septets.count - i - 1)
            guard hdr < 0x80, n > 0 else { return nil }
            for j in 0..<n {
                let b = septets[i + 1 + j]
                guard b < 0x80 else { return nil }
                out.append(b | (((hdr >> UInt8(j)) & 1) << 7))
            }
            i += 1 + n
        }
        return out
    }

    /// `(-(sum of bytes)) & 0x7F`, over `<ver>` through the last payload septet.
    public static func checksum<S: Sequence>(_ body: S) -> UInt8 where S.Element == UInt8 {
        var sum = 0
        for b in body { sum += Int(b) }
        return UInt8((-sum) & 0x7F)
    }
}

public struct SysExFrame: Equatable, Sendable {
    /// Request: target deck 0-15 or `Proto.SysEx.deckAny`. Reply: the replying unit's deck id.
    public var deck: UInt8
    /// Command, reply code, ACK (0x7E) or NAK (0x7F). See `kind`.
    public var code: UInt8
    /// Unpacked payload bytes.
    public var payload: [UInt8]

    public init(deck: UInt8, code: UInt8, payload: [UInt8] = []) {
        self.deck = deck
        self.code = code
        self.payload = payload
    }

    public init(deck: UInt8, command: SysExCmd, payload: [UInt8] = []) {
        self.init(deck: deck, code: command.rawValue, payload: payload)
    }

    public init(deck: UInt8, reply: SysExReply, payload: [UInt8]) {
        self.init(deck: deck, code: reply.rawValue, payload: payload)
    }

    public static func ack(deck: UInt8, command: UInt8) -> SysExFrame {
        SysExFrame(deck: deck, code: Proto.SysEx.ack, payload: [command])
    }

    public static func nak(deck: UInt8, command: UInt8, error: SysExError) -> SysExFrame {
        SysExFrame(deck: deck, code: Proto.SysEx.nak, payload: [command, error.rawValue])
    }

    public enum Kind: Equatable, Sendable {
        case command(SysExCmd)
        case reply(SysExReply)
        /// `command` is the acknowledged command id (payload[0]).
        case ack(command: UInt8)
        /// `errorCode` stays raw so a newer firmware's unknown code is not lost; see `SysExError(rawValue:)`.
        case nak(command: UInt8, errorCode: UInt8)
        /// Unknown code, or an ACK/NAK whose payload is too short to interpret.
        case unknown(code: UInt8)
    }

    public var kind: Kind {
        switch code {
        case Proto.SysEx.ack:
            return payload.count >= 1 ? .ack(command: payload[0]) : .unknown(code: code)
        case Proto.SysEx.nak:
            return payload.count >= 2 ? .nak(command: payload[0], errorCode: payload[1]) : .unknown(code: code)
        default:
            if let c = SysExCmd(rawValue: code) { return .command(c) }
            if let r = SysExReply(rawValue: code) { return .reply(r) }
            return .unknown(code: code)
        }
    }

    /// Complete `F0 ... F7` frame. `deck` and `code` must be 7-bit: a data byte >= 0x80 would
    /// terminate the SysEx on the wire, so that is a programmer error rather than something to mask.
    public func encode() -> [UInt8] {
        precondition(deck < 0x80 && code < 0x80, "SysEx deck and code must be 7-bit")
        var body: [UInt8] = [Proto.SysEx.version, deck, code]
        body += SysExCodec.pack8to7(payload)
        return [0xF0] + SysExCodec.header + body + [SysExCodec.checksum(body), 0xF7]
    }

    /// Strict decode of one complete `F0 ... F7` frame.
    public static func decode(_ bytes: [UInt8]) throws -> SysExFrame {
        guard bytes.count >= 2, bytes.first == 0xF0, bytes.last == 0xF7 else { throw SysExFrameError.missingFraming }
        guard bytes.count >= 4, Array(bytes[1...3]) == SysExCodec.header else { throw SysExFrameError.notEPDJ3000 }
        // F0 + header(3) + ver + deck + cmd + checksum + F7
        guard bytes.count >= 9 else { throw SysExFrameError.tooShort }
        for b in bytes[1..<(bytes.count - 1)] where b >= 0x80 { throw SysExFrameError.dataByteOutOfRange }
        guard bytes[4] == Proto.SysEx.version else { throw SysExFrameError.unsupportedVersion(bytes[4]) }
        let checksumIndex = bytes.count - 2
        guard SysExCodec.checksum(bytes[4..<checksumIndex]) == bytes[checksumIndex] else {
            throw SysExFrameError.badChecksum
        }
        guard let payload = SysExCodec.unpack7to8(Array(bytes[7..<checksumIndex])) else {
            throw SysExFrameError.badPacking
        }
        return SysExFrame(deck: bytes[5], code: bytes[6], payload: payload)
    }
}

import Foundation

/// Locates the repo from this file's path so the shared vectors are read from protocol/, not copied.
enum Repo {
    static let root: URL = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()  // EPDJ3000ProtocolTests
        .deletingLastPathComponent()  // Tests
        .deletingLastPathComponent()  // macos
        .deletingLastPathComponent()  // repo root

    static func decode<T: Decodable>(_ type: T.Type, _ relativePath: String) -> T {
        let url = root.appendingPathComponent(relativePath)
        do {
            return try JSONDecoder().decode(T.self, from: Data(contentsOf: url))
        } catch {
            fatalError("cannot load \(url.path): \(error)")
        }
    }
}

/// protocol/test-vectors.json
struct VectorFile: Decodable {
    struct Pack: Decodable { let raw: [UInt8]; let packed: [UInt8] }
    struct Frame: Decodable { let name: String; let deck: UInt8; let cmd: UInt8; let payload: [UInt8]; let frame: [UInt8] }
    struct StructVector: Decodable {
        let `struct`: String
        let values: [String: Value]
        let bytes: [UInt8]
    }
    enum Value: Decodable {
        case scalar(Int)
        case list([Int])
        init(from decoder: Decoder) throws {
            let c = try decoder.singleValueContainer()
            if let i = try? c.decode(Int.self) { self = .scalar(i) } else { self = .list(try c.decode([Int].self)) }
        }
    }
    struct Cc14: Decodable { let value14: Int; let msb: UInt8; let lsb: UInt8 }
    struct Rel: Decodable { let delta: Int; let value: UInt8 }
    struct Usb: Decodable { let name: String; let cable: UInt8; let message: [UInt8]; let packets: [[UInt8]] }
    struct Mtc: Decodable { let h: Int; let m: Int; let s: Int; let f: Int; let fpsType: Int; let quarterFrames: [[UInt8]]; let fullFrame: [UInt8] }
    struct Clock: Decodable { let bpmX10: Int; let periodUs: Double }

    let pack8to7: [Pack]
    let sysexFrames: [Frame]
    let structs: [StructVector]
    let cc14: [Cc14]
    let relative: [Rel]
    let usbMidi: [Usb]
    let mtc: [Mtc]
    let clockPeriod: [Clock]

    static let shared = Repo.decode(VectorFile.self, "protocol/test-vectors.json")

    func frame(_ name: String) -> Frame {
        guard let f = sysexFrames.first(where: { $0.name == name }) else { fatalError("no vector frame named \(name)") }
        return f
    }
}

extension VectorFile.StructVector {
    func int(_ key: String) -> Int {
        guard case let .scalar(v)? = values[key] else { fatalError("\(key) is not a scalar in \(`struct`)") }
        return v
    }

    func list(_ key: String) -> [Int] {
        guard case let .list(v)? = values[key] else { fatalError("\(key) is not a list in \(`struct`)") }
        return v
    }
}

/// The parts of protocol/epdj3000.json the Swift constants are checked against.
struct ContractFile: Decodable {
    struct Named: Decodable { let id: Int; let name: String }
    struct ConfigKey: Decodable { let id: Int; let name: String; let min: Int; let max: Int; let `default`: Int }
    struct Control: Decodable { let id: String; let number: Int; let lsb: Int? }
    struct Fps: Decodable { let id: Int; let name: String; let fps: Double }
    struct Midi: Decodable { let controls: [Control]; let mtcFpsTypes: [Fps] }
    struct SysEx: Decodable { let commands: [Named]; let replies: [Named]; let errors: [Named] }
    struct Config: Decodable { let keys: [ConfigKey] }

    let midi: Midi
    let sysex: SysEx
    let config: Config

    static let shared = Repo.decode(ContractFile.self, "protocol/epdj3000.json")
}

// swift-tools-version:5.9
import PackageDescription

// Protocol + Engine are pure Swift and must build and test on Linux as well as macOS.
// Kit (CoreMIDI/AVFoundation glue) and App (SwiftUI) import Apple frameworks, so they are
// only declared when the manifest itself is evaluated on macOS.
var targets: [Target] = [
    .target(name: "EPDJ3000Protocol"),
    .target(name: "EPDJ3000Engine", dependencies: ["EPDJ3000Protocol"]),
    .testTarget(name: "EPDJ3000ProtocolTests", dependencies: ["EPDJ3000Protocol"]),
    .testTarget(name: "EPDJ3000EngineTests", dependencies: ["EPDJ3000Engine", "EPDJ3000Protocol"]),
]

#if os(macOS)
targets += [
    .target(name: "EPDJ3000Kit", dependencies: ["EPDJ3000Engine", "EPDJ3000Protocol"]),
    .executableTarget(name: "EPDJ3000App", dependencies: ["EPDJ3000Kit", "EPDJ3000Engine", "EPDJ3000Protocol"]),
]
#endif

let package = Package(
    name: "EPDJ3000",
    platforms: [.macOS(.v13)],
    targets: targets
)

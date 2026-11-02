// swift-tools-version:5.9
import PackageDescription

// Protocol, Engine and KitPure are pure Swift and must build and test on Linux as well as macOS. KitPure is the part of
// the Kit that needs no Apple framework (Sources/EPDJ3000Kit/Pure); it is its own target so CI can test it. Kit
// (CoreMIDI/AVFoundation glue) and App (SwiftUI) import Apple frameworks, so they are only declared when the manifest
// itself is evaluated on macOS.
var targets: [Target] = [
    .target(name: "EPDJ3000Protocol"),
    .target(name: "EPDJ3000Engine", dependencies: ["EPDJ3000Protocol"]),
    .target(name: "EPDJ3000KitPure", dependencies: ["EPDJ3000Protocol"], path: "Sources/EPDJ3000Kit/Pure"),
    .testTarget(name: "EPDJ3000ProtocolTests", dependencies: ["EPDJ3000Protocol"]),
    .testTarget(name: "EPDJ3000EngineTests", dependencies: ["EPDJ3000Engine", "EPDJ3000Protocol"]),
    .testTarget(name: "EPDJ3000KitPureTests", dependencies: ["EPDJ3000KitPure", "EPDJ3000Protocol"]),
    // The real firmware Engine (firmware/core/tools/engine_sim.cpp, built by tools/e2e.sh) as a child process; skipped when it is not built.
    .testTarget(name: "EPDJ3000E2ETests", dependencies: ["EPDJ3000Engine", "EPDJ3000Protocol"]),
]

#if os(macOS)
targets += [
    .target(name: "EPDJ3000Kit", dependencies: ["EPDJ3000KitPure", "EPDJ3000Engine", "EPDJ3000Protocol"], exclude: ["Pure"]),
    .executableTarget(name: "EPDJ3000App", dependencies: ["EPDJ3000Kit", "EPDJ3000KitPure", "EPDJ3000Engine", "EPDJ3000Protocol"]),
]
#endif

let package = Package(
    name: "EPDJ3000",
    platforms: [.macOS(.v13)],
    targets: targets
)

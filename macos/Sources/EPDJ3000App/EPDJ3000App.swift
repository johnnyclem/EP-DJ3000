// EP-DJ3000 test app: calibration, MIDI monitor, a test player and a two-deck rig view for the EP-DJ3000.

import AppKit
import EPDJ3000Kit
import SwiftUI

/// `swift run` starts a bare executable, not an app bundle: without this it gets no menu bar and its window may not
/// come to the front. Harmless inside the .app that scripts/make-app.sh builds.
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }
}

@main
struct EPDJ3000App: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var manager = DeviceManager()

    var body: some Scene {
        Window("EP-DJ3000", id: "main") {
            RootView()
                .environmentObject(manager)
                .frame(minWidth: 980, minHeight: 640)
                .tint(.orange)
                .onAppear { manager.start() }
                .onReceive(NotificationCenter.default.publisher(for: NSApplication.willTerminateNotification)) { _ in
                    manager.shutdown()
                }
        }
        .defaultSize(width: 1100, height: 760)
    }
}

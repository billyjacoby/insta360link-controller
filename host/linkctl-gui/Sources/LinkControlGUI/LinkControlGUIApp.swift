import SwiftUI
import AVFoundation
import LinkControlCore

@MainActor final class AppDelegate: NSObject, NSApplicationDelegate {
    let controller = AppController()
    let preview = PreviewModel()
    private var terminationPending = false
    private var dashboardOpener: (() -> Void)?
    private var dashboardRequested = false
    private var previewAutomationTask: Task<Void, Never>?

    func applicationDidFinishLaunching(_ notification: Notification) {
        controller.applicationDidLaunch()
    }

    func application(_ application: NSApplication, open urls: [URL]) {
        // URL events may arrive before didFinishLaunching. Startup is idempotent.
        controller.applicationDidLaunch()
        for url in urls { route(url) }
    }

    func route(_ url: URL) {
        guard !terminationPending else { return }
        do {
            let action = try AutomationAction.parse(url)
            switch action {
            case .dashboard:
                openDashboard()
                controller.reportAutomation("Dashboard requested.")
            case .startPreview:
                previewAutomationTask?.cancel()
                if AVCaptureDevice.authorizationStatus(for: .video) == .notDetermined { openDashboard() }
                if !preview.isRunning && !preview.isStarting { preview.requestAndStart() }
                controller.reportAutomation("Starting preview…")
                previewAutomationTask = Task { [weak self] in
                    guard let self else { return }
                    let deadline = ContinuousClock.now.advanced(by: .seconds(60))
                    while self.preview.isStarting && ContinuousClock.now < deadline {
                        do { try await Task.sleep(for: .milliseconds(50)) } catch { return }
                    }
                    guard !Task.isCancelled else { return }
                    if self.preview.isStarting {
                        self.preview.stop()
                        self.controller.reportAutomation("Preview start timed out. Open the dashboard and check camera permission.", failed: true)
                    } else {
                        self.controller.reportAutomation(self.preview.message, failed: !self.preview.isRunning)
                    }
                }
            case .stopPreview:
                previewAutomationTask?.cancel()
                preview.stop()
                controller.reportAutomation("Preview stopped.")
            default: controller.submitAutomation(action)
            }
        } catch { controller.reportAutomation(error.localizedDescription, failed: true) }
    }

    func installDashboardOpener(_ opener: @escaping () -> Void) {
        dashboardOpener = opener
        if dashboardRequested { dashboardRequested = false; opener() }
    }

    func openDashboard() {
        NSApp.activate(ignoringOtherApps: true)
        if let dashboardOpener { dashboardOpener() }
        else { dashboardRequested = true }
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        openDashboard()
        return true
    }

    func openRaycastScriptsFolder() {
        guard let folder = Bundle.main.resourceURL?.appendingPathComponent("Raycast", isDirectory: true),
              FileManager.default.fileExists(atPath: folder.path) else {
            controller.reportAutomation("Raycast scripts are missing from this development bundle. Use the packaged Link Control app to open its bundled scripts folder.", failed: true)
            return
        }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(folder.path, forType: .string)
        if NSWorkspace.shared.open(folder) {
            controller.reportAutomation("Raycast scripts opened; folder path copied. Add this directory in Raycast's Script Commands settings.")
        } else { controller.reportAutomation("Could not open Raycast scripts in Finder. The folder path was copied to the clipboard.", failed: true) }
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard !terminationPending else { return .terminateLater }
        terminationPending = true
        previewAutomationTask?.cancel()
        Task { @MainActor in
            async let stopPreview: Void = preview.shutdown()
            await controller.shutdown()
            await stopPreview
            sender.reply(toApplicationShouldTerminate: true)
        }
        return .terminateLater
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }
}

@main struct LinkControlGUIApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate

    var body: some Scene {
        Window("Link Control", id: "dashboard") {
            DashboardRoot(delegate: delegate)
                .environmentObject(delegate.controller)
                .environmentObject(delegate.preview)
        }
        .defaultSize(width: 1100, height: 820)
        MenuBarExtra {
            AutomationMenu(delegate: delegate)
                .environmentObject(delegate.controller)
                .environmentObject(delegate.preview)
        } label: {
            AutomationMenuLabel(controller: delegate.controller, delegate: delegate)
        }
    }
}

private struct DashboardRoot: View {
    let delegate: AppDelegate
    @Environment(\.openWindow) private var openWindow
    var body: some View {
        ContentView().onAppear {
            delegate.installDashboardOpener { openWindow(id: "dashboard") }
        }
    }
}

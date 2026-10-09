import SwiftUI
import LinkControlCore

struct AutomationMenuLabel: View {
    @ObservedObject var controller: AppController
    let delegate: AppDelegate
    @Environment(\.openWindow) private var openWindow
    var body: some View {
        Label("Link Control", systemImage: controller.automationFailed ? "exclamationmark.triangle" : "video")
            .onAppear { delegate.installDashboardOpener { openWindow(id: "dashboard") } }
    }
}

struct AutomationMenu: View {
    let delegate: AppDelegate
    @EnvironmentObject private var controller: AppController
    @EnvironmentObject private var preview: PreviewModel

    private func action(_ route: String) { delegate.route(URL(string: "linkcontrol://action/" + route)!) }

    var body: some View {
        Text(controller.statusLabel)
        Text(controller.automationMessage)
        Divider()
        Menu("Jog") {
            ForEach(AutomationAction.Jog.allCases, id: \.self) { direction in
                Button(direction.rawValue.capitalized) { action("jog?direction=\(direction.rawValue)") }
            }
        }.disabled(!controller.automationCameraReady || !controller.canJog)
        Button("Center Camera") { action("center") }.disabled(!controller.automationCameraReady || !controller.canJog)
        Button("Stop Manual Motion") { action("stop") }
        Menu("Zoom") {
            ForEach([100, 150, 200, 300, 400], id: \.self) { value in
                Button("\(value)%") { action("zoom?value=\(value)") }
            }
        }.disabled(!controller.automationCameraReady || !controller.canZoom)
        Menu("Camera Mode") {
            ForEach(AutomationAction.Mode.allCases, id: \.self) { mode in
                Button((controller.aiMode?.value == mode.value ? "✓ " : "") + (mode == .deskview ? "DeskView" : mode.rawValue.capitalized)) {
                    action("mode?value=\(mode.rawValue)")
                }
                .disabled(controller.aiMode?.accepts(mode.value) != true)
            }
        }.disabled(!controller.automationCameraReady || !controller.canEditSettings || controller.aiMode?.canEdit != true)
        Divider()
        Text(preview.isRunning ? "Preview live — camera stream stays active with window closed" : preview.isStarting ? "Preview starting…" : "Preview off")
        Button("Start Preview") { action("start-preview") }
            .disabled(preview.isRunning || preview.isStarting || preview.devices.isEmpty)
        Button("Stop Preview") { action("stop-preview") }
            .disabled(!preview.isRunning && !preview.isStarting)
        Button("Refresh") { action("refresh") }.disabled(!controller.canRefreshSettings)
        Divider()
        Button("Open Dashboard") { action("dashboard") }
        Button("Open Raycast Scripts Folder") { delegate.openRaycastScriptsFolder() }
        Divider()
        Button("Quit Link Control") { NSApp.terminate(nil) }.keyboardShortcut("q")
    }
}

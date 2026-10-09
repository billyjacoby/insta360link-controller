import SwiftUI
import LinkControlCore

struct ContentView: View {
    @EnvironmentObject private var controller: AppController
    @EnvironmentObject private var preview: PreviewModel
    @State private var zoomDraft = 100.0
    @State private var zoomEdited = false
    @State private var showLogs = false

    var body: some View {
        NavigationSplitView {
            sidebar
                .navigationTitle("Link Control")
                .navigationSplitViewColumnWidth(min: 245, ideal: 280)
        } detail: {
            VStack(spacing: 0) {
                VStack(alignment: .leading, spacing: 16) {
                    header
                    previewPanel
                }
                .padding(.horizontal, 24)
                .padding(.top, 16)
                .padding(.bottom, 12)

                Divider()

                ScrollView {
                    VStack(alignment: .leading, spacing: 20) {
                        controls
                        CameraSettingsView()
                        logPanel
                    }
                    .padding(24)
                }
            }
        }
        .frame(minWidth: 900, minHeight: 680)
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didResignActiveNotification)) { _ in
            controller.stopMotion()
        }
        .onChange(of: controller.reportedZoom) { value in
            if !zoomEdited { zoomDraft = Double(value) }
        }
        .onChange(of: controller.controlsEnabled) { enabled in
            if !enabled {
                zoomEdited = false
                zoomDraft = Double(controller.reportedZoom)
            }
        }
    }

    private var sidebar: some View {
        List {
            Section("CAMERA") {
                Label(controller.statusLabel, systemImage: "circle.fill")
                    .foregroundStyle(statusColor)
                    .font(.callout)
                Text(controller.statusDetail)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Button("Retry connection") { controller.retryConnection() }
                    .disabled(!controller.canRetryConnection)
            }
            Section("SUPPORTED CAMERAS") {
                Text("Controls automatically detect Insta360 Link and Link 2 Pro over USB.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Text("Live preview can use any macOS camera. Selecting a preview camera does not change which supported Insta360 camera receives controls.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Text("Start preview to wake Link 2 Pro before using controls. In standby it may acknowledge commands without applying them.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Section("AUTOMATION") {
                Label(controller.automationMessage, systemImage: controller.automationFailed ? "exclamationmark.triangle" : "bolt")
                    .font(.caption)
                    .foregroundStyle(controller.automationFailed ? .orange : .secondary)
                Text("Closing this window keeps Link Control in the menu bar. Quit there to stop the helper.")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
        .listStyle(.sidebar)
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Camera dashboard").font(.largeTitle.bold())
            Text("Connect your camera, start a preview, and adjust your shot.")
                .foregroundStyle(.secondary)
        }
    }

    private var previewPanel: some View {
        GroupBox("Live preview") {
            VStack(alignment: .leading, spacing: 12) {
                ZStack {
                    Color.black.opacity(0.9)
                    if preview.isRunning {
                        CameraPreview(session: preview.session)
                    } else {
                        VStack(spacing: 10) {
                            Image(systemName: "video").font(.system(size: 36))
                            Text(preview.isStarting ? "Starting preview…" : "Preview is off")
                                .font(.headline)
                            Text("Camera permission is requested when you start preview.")
                                .font(.caption)
                        }
                        .foregroundStyle(.white.opacity(0.8))
                    }
                }
                .frame(height: 270)
                .clipShape(RoundedRectangle(cornerRadius: 8))
                .accessibilityLabel(preview.isRunning ? "Live camera preview" : "Camera preview is off")
                HStack {
                    Picker("Camera", selection: $preview.selectedID) {
                        if preview.devices.isEmpty { Text("No cameras").tag("") }
                        ForEach(preview.devices, id: \.uniqueID) { device in
                            Text(device.localizedName).tag(device.uniqueID)
                        }
                    }
                    Button(preview.isRunning || preview.isStarting ? "Stop Preview" : "Start Preview") {
                        if preview.isRunning || preview.isStarting { preview.stop() }
                        else { preview.requestAndStart() }
                    }
                    .disabled(preview.devices.isEmpty)
                    Button("Refresh") { preview.refreshDevices() }
                }
                Text(preview.message).font(.caption).foregroundStyle(.secondary)
                if preview.isRunning {
                    Text("The camera stream stays active when the window is closed. Choose Stop Preview to release it.")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
            .padding(8)
        }
    }

    private var controls: some View {
        GroupBox("Camera controls") {
            HStack(alignment: .center, spacing: 30) {
                motionPad
                VStack(alignment: .leading, spacing: 12) {
                    Text("Jog speed: \(Int(controller.speed))").font(.headline)
                    Slider(value: $controller.speed, in: 1...20, step: 1)
                        .accessibilityLabel("Jog speed")
                    Button("Center camera") { controller.center() }
                        .help("Send center, then stop after 500 milliseconds.")
                        .disabled(!controller.canJog)
                    if controller.manualMotionBlocked {
                        Text("Select Normal mode before jogging or centering the camera.")
                            .font(.caption).foregroundStyle(.secondary)
                    } else if controller.modePending {
                        Text("Camera mode is settling. Stop manual motion remains available.")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                    Divider()
                    Text("Zoom: \(Int(zoomDraft))% (\(Double(Int(zoomDraft)) / 100, specifier: "%.2f")×)")
                        .font(.headline)
                    Slider(value: Binding(
                        get: { zoomDraft },
                        set: { zoomDraft = $0; zoomEdited = true }
                    ), in: 100...400, step: 1)
                    .accessibilityLabel("Zoom percentage")
                    HStack {
                        Button("Apply Zoom") {
                            controller.setZoom(Int(zoomDraft))
                            zoomEdited = false
                        }
                        .disabled(!controller.canZoom)
                        Text("One command per apply").font(.caption).foregroundStyle(.secondary)
                    }
                }
                .frame(maxWidth: 310)
                .disabled(!controller.controlsEnabled)
                Spacer(minLength: 0)
            }
            .padding(12)
        }
    }

    private var motionPad: some View {
        VStack(spacing: 8) {
            jogButton("Jog up", icon: "chevron.up", tilt: .negative)
            HStack(spacing: 8) {
                jogButton("Jog left", icon: "chevron.left", pan: .negative)
                Button { controller.submitAutomation(.stop) } label: {
                    Image(systemName: "stop.fill").frame(width: 32, height: 32)
                }
                .buttonStyle(.borderedProminent)
                .tint(.red)
                .accessibilityLabel("Stop manual motion")
                .help("Stop a manual jog. Select Normal camera mode to turn off AI tracking.")
                .disabled(controller.state != .connected)
                jogButton("Jog right", icon: "chevron.right", pan: .positive)
            }
            jogButton("Jog down", icon: "chevron.down", tilt: .positive)
            Text("Click to jog · 350 ms per click")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }

    private func jogButton(
        _ label: String, icon: String, pan: Direction = .stopped, tilt: Direction = .stopped
    ) -> some View {
        Button { controller.move(pan: pan, tilt: tilt) } label: {
            Image(systemName: icon).frame(width: 32, height: 32)
        }
        .buttonStyle(.bordered)
        .accessibilityLabel(label)
        .accessibilityHint("Sends a 350 millisecond jog, followed by stop.")
        .help("\(label) for 350 milliseconds")
        .disabled(!controller.canJog)
    }

    private var logPanel: some View {
        DisclosureGroup("Connection diagnostics", isExpanded: $showLogs) {
            VStack(alignment: .leading) {
                ScrollView([.horizontal, .vertical]) {
                    Text(controller.logs.isEmpty ? "No log messages yet." : controller.logs)
                        .font(.system(.caption, design: .monospaced))
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                .frame(height: 150)
                HStack {
                    Text("Last 65,536 characters · includes camera helper output")
                        .font(.caption).foregroundStyle(.secondary)
                    Spacer()
                    Button("Clear") { controller.clearLogs() }
                }
            }
            .padding(.top, 8)
        }
    }

    private var statusColor: Color {
        switch controller.state {
        case .connected: controller.cameraConnected ? .green : .orange
        case .connecting, .disconnecting: .orange
        case .offline: .secondary
        case .error: .red
        }
    }

}

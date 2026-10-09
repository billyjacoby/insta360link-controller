import SwiftUI
import LinkControlCore

struct CameraSettingsView: View {
    @EnvironmentObject private var controller: AppController

    var body: some View {
        GroupBox("Camera settings") {
            VStack(alignment: .leading, spacing: 16) {
                HStack {
                    Text("Changes are applied directly to your camera. Controls use its reported capabilities and supported protocol options.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Spacer(minLength: 12)
                    Button("Refresh") { controller.refreshSettings() }
                        .disabled(!controller.canRefreshSettings)
                        .accessibilityLabel("Refresh camera settings")
                }

                if controller.isSwitchingMode {
                    ProgressView("Waiting for camera mode…").controlSize(.small)
                } else if controller.isLoadingSettings {
                    ProgressView("Reading camera settings…").controlSize(.small)
                } else if let id = controller.savingSettingID {
                    ProgressView("Saving \(controller.settings.first(where: { $0.id == id })?.label ?? "setting")…")
                        .controlSize(.small)
                }
                if let error = controller.settingsError {
                    Label(error, systemImage: "exclamationmark.triangle")
                        .font(.callout)
                        .foregroundStyle(.orange)
                }

                if controller.settings.isEmpty && !controller.isLoadingSettings {
                    Text(controller.controlsEnabled
                         ? "No supported settings were reported by this camera."
                         : "Connect your camera to discover its settings.")
                        .foregroundStyle(.secondary)
                }

                ForEach(CameraSetting.Group.allCases, id: \.self) { group in
                    let controls = controller.settings.filter { $0.group == group }
                    if !controls.isEmpty {
                        VStack(alignment: .leading, spacing: 12) {
                            Text(group.title).font(.headline)
                            if group == .ai {
                                Text("Keep preview running when changing camera modes. Use Refresh to read changes made by gestures or another controller.")
                                    .font(.caption).foregroundStyle(.secondary)
                            }
                            ForEach(controls) { control in
                                CameraSettingRow(control: control)
                            }
                            if group == .ai {
                                Text("Tracking needs a person in view. DeskView rotates video; use the jog controls to tilt down after switching. Overhead moves the camera down automatically. Whiteboard detects a board already in frame.")
                                    .font(.caption).foregroundStyle(.secondary)
                                Text("Tracking speed, framing, target selection, and gesture configuration still need verified Pro commands. This controls the camera’s Whiteboard mode, without the official app’s Virtual Camera features.")
                                    .font(.caption).foregroundStyle(.secondary)
                            }
                        }
                    }
                }
                .id(controller.settingsEpoch)
            }
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }
}

private struct CameraSettingRow: View {
    @EnvironmentObject private var controller: AppController
    let control: CameraSetting
    @State private var draft: SettingDraft

    init(control: CameraSetting) {
        self.control = control
        _draft = State(initialValue: SettingDraft(control: control))
    }

    private var enabled: Bool { controller.canEditSettings && control.canEdit }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(alignment: .firstTextBaseline) {
                Text(control.label).font(.callout.weight(.medium))
                Spacer()
                Text(control.formattedValue(control.value))
                    .font(.callout.monospacedDigit())
                    .foregroundStyle(.secondary)
                if control.canReset {
                    Button("Reset") {
                        draft.submitted()
                        controller.resetSetting(id: control.id)
                    }
                    .disabled(!enabled || !control.canReset)
                    .help("Restore \(control.formattedValue(control.defaultValue))")
                    .accessibilityLabel("Reset \(control.label) to default")
                }
            }

            // Read-only values can be outside the advertised range/options.
            // Display their report above without constructing an invalid editor.
            if control.writable { editor.disabled(!enabled) }

            if let error = control.error {
                Text(error).font(.caption).foregroundStyle(.orange)
            }
            if let detail = control.detail {
                Text(detail).font(.caption).foregroundStyle(.secondary)
            }
            if control.id == "ai_mode" && (control.isPending || !control.active) {
                Text(control.isPending ? "Waiting for camera mode change…" : "Camera mode is currently unavailable.")
                    .font(.caption).foregroundStyle(.secondary)
            } else if !control.active {
                Text("Unavailable in the current camera mode. Change its automatic mode to enable manual adjustment, when supported.")
                    .font(.caption).foregroundStyle(.secondary)
            } else if !control.writable {
                Text("Read-only on this camera.").font(.caption).foregroundStyle(.secondary)
            } else if !control.readable {
                Text("The camera does not report the current value.").font(.caption).foregroundStyle(.secondary)
            }
        }
        .onChange(of: control) { updated in
            // Status polling never replaces this model. Preserve unsaved drafts
            // across other setting writes; discard only an invalidated range.
            draft.reconcile(updated)
        }
        .onChange(of: controller.savingSettingID) { saving in
            if saving == nil && controller.settingsError == nil { draft.reconcile(control) }
        }
    }

    @ViewBuilder private var editor: some View {
        switch control.kind {
        case .integer:
            HStack(spacing: 12) {
                if control.min < control.max {
                    Slider(value: Binding(
                        // A new range may render before onChange reconciles the
                        // old draft, including a read-only -> writable transition.
                        get: { Double(min(control.max, max(control.min, draft.value))) },
                        set: { draft.edit(Int($0.rounded())) }
                    ), in: Double(control.min)...Double(control.max), step: Double(control.step))
                    .accessibilityLabel(control.label)
                }
                Text(control.formattedValue(draft.value)).monospacedDigit().frame(minWidth: 70, alignment: .trailing)
                Button("Apply") {
                    draft.submitted()
                    controller.setSetting(id: control.id, value: draft.value)
                }
                .disabled(!control.accepts(draft.value) || draft.value == control.value)
                .accessibilityLabel("Apply \(control.label)")
            }
            Text("\(control.formattedValue(control.min)) – \(control.formattedValue(control.max)) · Step \(control.step)")
                .font(.caption).foregroundStyle(.secondary)
        case .boolean:
            if let value = control.value {
                Toggle("Enabled", isOn: Binding(
                    get: { value != 0 },
                    set: { controller.setSetting(id: control.id, value: $0 ? 1 : 0) }
                ))
                .toggleStyle(.switch)
                .accessibilityLabel(control.label)
            } else {
                // Do not display an unknown value as a confirmed Off state.
                Picker(control.label, selection: Binding<Int?>(
                    get: { control.value },
                    set: { if let value = $0 { controller.setSetting(id: control.id, value: value) } }
                )) {
                    Text("Unavailable").tag(nil as Int?)
                    Text("Off").tag(Int?.some(0))
                    Text("On").tag(Int?.some(1))
                }
                .labelsHidden()
            }
        case .enumeration:
            Picker(control.label, selection: Binding<Int?>(
                get: { control.value },
                set: { if let value = $0 { controller.setSetting(id: control.id, value: value) } }
            )) {
                if control.value == nil { Text("Unavailable").tag(nil as Int?) }
                ForEach(control.options) { option in
                    Text(option.label).tag(Int?.some(option.value))
                }
            }
            .labelsHidden()
        }
    }
}

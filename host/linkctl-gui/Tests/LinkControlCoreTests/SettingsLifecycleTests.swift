import XCTest
@testable import LinkControlCore
@testable import LinkControlGUI

@MainActor final class SettingsLifecycleTests: XCTestCase {
    private func waitUntil(timeout: TimeInterval = 3, _ predicate: @escaping @MainActor () -> Bool) async throws {
        let deadline = Date().addingTimeInterval(timeout)
        while !predicate() {
            if Date() > deadline {
                XCTFail("Condition did not become true within \(timeout) seconds")
                throw TestError.timeout
            }
            try await Task.sleep(for: .milliseconds(10))
        }
    }

    func testSettingsCannotEstablishInitialConnectionReadiness() async throws {
        let server = try MockDaemon(mode: .settingsOnly)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url, timeout: .milliseconds(200)))
        controller.connect(attempts: 1)
        try await waitUntil { server.snapshot().contains("reply:status") }
        XCTAssertEqual(controller.state, .connecting)
        XCTAssertTrue(controller.settings.isEmpty)
        try await waitUntil { if case .error = controller.state { return true }; return false }
        await controller.shutdown()
    }

    func testAIModeUsesReadbackAndDoesNotPretendFailedWriteWasApplied() async throws {
        let snapshot = AIModeFixtures.snapshot(0)
        let server = try MockDaemon(settingsReply: snapshot)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.settings.count == 1 && !controller.settingsBusy }
        controller.setSetting(id: "ai_mode", value: 1)
        try await waitUntil { !controller.settingsBusy }
        XCTAssertEqual(controller.settings.first?.value, 1)
        controller.setSetting(id: "ai_mode", value: 0)
        try await waitUntil { !controller.settingsBusy }
        XCTAssertEqual(controller.settings.first?.value, 0)
        server.setSettingsResponse(#"{"type":"settings_error","message":"Camera did not apply tracking change"}"#)
        controller.setSetting(id: "ai_mode", value: 1)
        try await waitUntil { controller.settingsError != nil }
        XCTAssertEqual(controller.settings.first?.value, 0)
        XCTAssertEqual(controller.state, .connected)
        XCTAssertTrue(controller.canRefreshSettings)
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") }, [
            "setting_set:ai_mode=1", "setting_set:ai_mode=0", "setting_set:ai_mode=1"
        ])
        await controller.shutdown()
    }

    func testInitialDiscoveryExplicitRefreshAndStatusPollingDoNotRepeatedlyReadSettings() async throws {
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.settings.count == 3 && !controller.settingsBusy }
        try await waitUntil { server.snapshot().filter { $0 == "status" }.count >= 2 }
        XCTAssertEqual(server.snapshot().filter { $0 == "settings_get" }.count, 1)
        XCTAssertEqual(controller.settings.first?.id, "brightness")
        server.setSettingsResponse(MockDaemon.defaultSettings.replacingOccurrences(of: "\"value\":0", with: "\"value\":16"))
        controller.refreshSettings()
        XCTAssertTrue(controller.isLoadingSettings)
        controller.refreshSettings()
        try await waitUntil { !controller.settingsBusy }
        XCTAssertEqual(controller.settings.first?.value, 16)
        XCTAssertEqual(server.snapshot().filter { $0 == "settings_get" }.count, 2)
        await controller.shutdown()
    }

    func testSuccessfulWriteConsumesFreshSnapshotAndUpdatesAutomaticDependencies() async throws {
        let server = try MockDaemon(settingsDelay: 0.08)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { !controller.settings.isEmpty && !controller.settingsBusy }
        controller.setSetting(id: "focus_absolute", value: 80) // Inactive while autofocus is enabled.
        controller.setSetting(id: "brightness", value: 65) // Outside the advertised range.
        controller.setSetting(id: "exposure_absolute", value: 10) // Not advertised.
        controller.setSetting(id: "focus_auto", value: 0)
        XCTAssertEqual(controller.savingSettingID, "focus_auto")
        controller.setSetting(id: "focus_auto", value: 1) // No duplicate writes while pending.
        controller.refreshSettings()
        try await waitUntil { !controller.settingsBusy }
        XCTAssertEqual(controller.settings.first { $0.id == "focus_auto" }?.value, 0)
        XCTAssertTrue(try XCTUnwrap(controller.settings.first { $0.id == "focus_absolute" }).active)
        XCTAssertEqual(server.snapshot().filter { $0 == "setting_set" }.count, 1)
        XCTAssertEqual(server.snapshot().filter { $0 == "settings_get" }.count, 1,
                       "setting_set itself returns the full fresh snapshot")
        controller.setSetting(id: "focus_absolute", value: 80)
        try await waitUntil { !controller.settingsBusy }
        XCTAssertEqual(controller.settings.first { $0.id == "focus_absolute" }?.value, 80)
        await controller.shutdown()
    }

    func testResetUsesAdvertisedDefaultAndHonorsWritableActiveFlags() async throws {
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { !controller.settings.isEmpty && !controller.settingsBusy }
        controller.resetSetting(id: "focus_absolute")
        controller.resetSetting(id: "unknown")
        controller.setSetting(id: "brightness", value: 32)
        try await waitUntil { !controller.settingsBusy }
        controller.resetSetting(id: "brightness")
        try await waitUntil { !controller.settingsBusy }
        XCTAssertEqual(controller.settings.first?.value, 0)
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") },
                       ["setting_set:brightness=32", "setting_set:brightness=0"])
        let readOnly = MockDaemon.defaultSettings.replacingOccurrences(of: "\"writable\":true", with: "\"writable\":false")
        server.setSettingsResponse(readOnly)
        controller.refreshSettings()
        try await waitUntil { !controller.settingsBusy }
        controller.resetSetting(id: "brightness")
        controller.setSetting(id: "brightness", value: 10)
        XCTAssertNil(controller.savingSettingID)
        XCTAssertEqual(server.snapshot().filter { $0 == "setting_set" }.count, 2)
        await controller.shutdown()
    }

    func testSettingsReadAndWriteFailuresAreNonfatalAndRefreshRecovers() async throws {
        let server = try MockDaemon(settingsReply: #"{"type":"settings_error","message":"USB read failed"}"#)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.settingsError != nil }
        XCTAssertEqual(controller.settingsError, "USB read failed")
        XCTAssertEqual(controller.state, .connected)
        XCTAssertTrue(controller.cameraConnected)
        XCTAssertTrue(controller.canJog)
        controller.setZoom(150)
        try await waitUntil { server.snapshot().contains("reply:zoom") }
        server.setSettingsResponse(MockDaemon.defaultSettings)
        controller.refreshSettings()
        try await waitUntil { !controller.settingsBusy }
        XCTAssertNil(controller.settingsError)
        XCTAssertEqual(controller.settings.count, 3)
        server.setSettingsResponse(#"{"type":"settings_error","message":"Write refused"}"#)
        controller.setSetting(id: "brightness", value: 8)
        try await waitUntil { controller.settingsError == "Write refused" }
        XCTAssertEqual(controller.state, .connected)
        XCTAssertTrue(controller.cameraConnected)
        XCTAssertEqual(controller.settings.first?.value, 0)
        XCTAssertFalse(controller.settingsBusy)
        controller.center()
        try await waitUntil { server.snapshot().contains("center") }
        await controller.shutdown()
    }

    func testPro2ReadOnlyOutlierCannotWriteAndAutoTogglesRestoreManualControls() async throws {
        let server = try MockDaemon(settingsReply: CameraSettingsFixtures.link2Pro())
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.settings.count == 10 && !controller.settingsBusy }
        XCTAssertNil(controller.settingsError)
        controller.setSetting(id: "power_line_frequency", value: 3)
        controller.setSetting(id: "power_line_frequency", value: 2)
        controller.resetSetting(id: "power_line_frequency")
        controller.setSetting(id: "focus_absolute", value: 80)
        controller.setSetting(id: "white_balance_temperature", value: 5500)
        XCTAssertFalse(controller.settingsBusy)
        XCTAssertFalse(server.snapshot().contains("setting_set"))

        for (autoID, manualID, newValue) in [
            ("focus_auto", "focus_absolute", 80),
            ("white_balance_auto", "white_balance_temperature", 5500)
        ] {
            let before = try XCTUnwrap(controller.settings.first { $0.id == manualID })
            XCTAssertFalse(before.writable)
            XCTAssertFalse(before.active)
            controller.setSetting(id: autoID, value: 0)
            try await waitUntil { !controller.settingsBusy }
            let after = try XCTUnwrap(controller.settings.first { $0.id == manualID })
            XCTAssertTrue(after.writable)
            XCTAssertTrue(after.active)
            XCTAssertTrue(after.canEdit)
            controller.setSetting(id: manualID, value: newValue)
            try await waitUntil { !controller.settingsBusy }
            XCTAssertEqual(controller.settings.first { $0.id == manualID }?.value, newValue)
        }
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") }, [
            "setting_set:focus_auto=0", "setting_set:focus_absolute=80",
            "setting_set:white_balance_auto=0", "setting_set:white_balance_temperature=5500"
        ])
        XCTAssertEqual(controller.settings.count, 10)
        XCTAssertEqual(controller.settings.first { $0.id == "power_line_frequency" }?.value, 3)
        XCTAssertNil(controller.settingsError)
        await controller.shutdown()
    }

    func testMalformedSettingsSnapshotIsIsolatedFromPTZ() async throws {
        let bad = MockDaemon.defaultSettings.replacingOccurrences(of: "\"step\":1", with: "\"step\":0")
        let server = try MockDaemon(settingsReply: bad)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.settingsError != nil }
        XCTAssertTrue(controller.settings.isEmpty)
        XCTAssertEqual(controller.state, .connected)
        XCTAssertTrue(controller.controlsEnabled)
        controller.move(tilt: .negative)
        try await waitUntil { server.snapshot().contains("pan_tilt") }
        await controller.shutdown()
    }

    func testAcknowledgementAndStatusDoNotCompletePendingSettingsRequest() async throws {
        let server = try MockDaemon(settingsDelay: 0.25, settingsPreamble: [
            #"{"type":"ack"}"#, #"{"type":"status","camera_connected":true,"zoom":200}"#
        ])
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.reportedZoom == 200 }
        XCTAssertTrue(controller.isLoadingSettings)
        XCTAssertTrue(controller.settings.isEmpty)
        controller.setZoom(175)
        try await Task.sleep(for: .milliseconds(60))
        XCTAssertFalse(server.snapshot().contains("zoom"), "The FIFO must still be waiting for settings")
        try await waitUntil { server.snapshot().contains("reply:zoom") }
        XCTAssertEqual(controller.settings.count, 3)
        await controller.shutdown()
    }

    func testSettingsAndSettingsErrorCannotAcknowledgePTZOrRejectIt() async throws {
        let server = try MockDaemon(ackDelay: 0.2)
        let url = try await server.start()
        defer { server.stop() }
        let connection = DaemonConnection(url: url)
        _ = try await connection.connect()
        await connection.receiveReplies { _ in }
        var completed = false
        let send = Task {
            let reply = try await connection.send(.stop)
            completed = true
            return reply
        }
        try await waitUntil { server.snapshot().contains("stop") }
        server.broadcast(MockDaemon.defaultSettings)
        server.broadcast(#"{"type":"settings_error","message":"unrelated"}"#)
        server.broadcast(#"{"type":"status","camera_connected":true,"zoom":125}"#)
        try await Task.sleep(for: .milliseconds(60))
        XCTAssertFalse(completed)
        let reply = try await send.value
        XCTAssertEqual(reply, .acknowledgement)
        await connection.disconnect()
    }

    func testNoDiscoveryWithoutCameraAndReconnectAutomaticallyFetchesOnce() async throws {
        let server = try MockDaemon(cameraConnected: false)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.state == .connected }
        controller.refreshSettings()
        XCTAssertFalse(controller.isLoadingSettings)
        XCTAssertFalse(server.snapshot().contains("settings_get"))
        server.setCameraConnected(true)
        try await waitUntil { controller.settings.count == 3 }
        XCTAssertEqual(server.snapshot().filter { $0 == "settings_get" }.count, 1)
        server.setCameraConnected(false)
        try await waitUntil { !controller.cameraConnected }
        XCTAssertTrue(controller.settings.isEmpty)
        XCTAssertNil(controller.settingsError)
        server.setCameraConnected(true)
        try await waitUntil { controller.settings.count == 3 }
        XCTAssertEqual(server.snapshot().filter { $0 == "settings_get" }.count, 2)
        await controller.shutdown()
    }

    func testCameraEpochRejectsOldSnapshotEvenAfterCameraHasReconnected() async throws {
        let server = try MockDaemon(settingsDelay: 0.25)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { server.snapshot().contains("settings_get") }
        server.setCameraConnected(false)
        try await waitUntil { !controller.cameraConnected }
        XCTAssertFalse(controller.settingsBusy)
        server.setSettingsResponse(MockDaemon.defaultSettings.replacingOccurrences(of: "\"value\":0", with: "\"value\":24"))
        server.setCameraConnected(true)
        try await waitUntil { controller.isLoadingSettings }
        try await waitUntil { server.snapshot().filter { $0 == "settings_get" }.count == 2 }
        XCTAssertTrue(controller.settings.isEmpty, "Old camera's reply cannot repopulate new camera settings")
        XCTAssertTrue(controller.isLoadingSettings)
        try await waitUntil { !controller.settingsBusy }
        XCTAssertEqual(controller.settings.first?.value, 24)
        XCTAssertEqual(controller.state, .connected)
        await controller.shutdown()
    }

    func testDisconnectClearsSettingsAndLateWriteCannotReviveThem() async throws {
        let server = try MockDaemon(settingsDelay: 0.15)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { !controller.settings.isEmpty && !controller.settingsBusy }
        controller.setSetting(id: "brightness", value: 24)
        try await waitUntil { server.snapshot().contains("setting_set") }
        let disconnected = controller.disconnect()
        XCTAssertTrue(controller.settings.isEmpty)
        XCTAssertFalse(controller.settingsBusy)
        await disconnected.value
        XCTAssertEqual(controller.state, .offline)
        XCTAssertTrue(controller.settings.isEmpty)
        XCTAssertNil(controller.settingsError)
        controller.connect(attempts: 1)
        try await waitUntil { !controller.settings.isEmpty && !controller.settingsBusy }
        XCTAssertEqual(controller.settings.first?.value, 24)
        await controller.shutdown()
    }

    func testMotionAndSettingsAreMutuallyExclusiveAndStopPrecedesNextRefresh() async throws {
        let server = try MockDaemon(settingsDelay: 0.12)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.isLoadingSettings }
        XCTAssertFalse(controller.canJog)
        controller.move(tilt: .negative)
        controller.center()
        try await waitUntil { !controller.settingsBusy }
        XCTAssertFalse(server.snapshot().contains("pan_tilt"))
        XCTAssertFalse(server.snapshot().contains("center"))
        controller.move(tilt: .negative)
        XCTAssertTrue(controller.motionActive)
        XCTAssertFalse(controller.canRefreshSettings)
        controller.refreshSettings()
        controller.setSetting(id: "brightness", value: 10)
        XCTAssertFalse(controller.settingsBusy)
        try await waitUntil { server.snapshot().contains("pan_tilt") }
        controller.stopMotion()
        controller.refreshSettings()
        try await waitUntil { !controller.settingsBusy }
        let events = server.snapshot()
        let stopIndex = try XCTUnwrap(events.firstIndex(of: "reply:stop"))
        let refreshIndex = try XCTUnwrap(events.lastIndex(of: "settings_get"))
        XCTAssertLessThan(stopIndex, refreshIndex)
        XCTAssertEqual(events.filter { $0 == "settings_get" }.count, 2)
        XCTAssertFalse(events.contains("setting_set"))
        await controller.shutdown()
    }

    private enum TestError: Error { case timeout }
}

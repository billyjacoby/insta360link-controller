import XCTest
@testable import LinkControlCore
@testable import LinkControlGUI

enum AIModeFixtures {
    static func snapshot(_ value: Int, pending: Bool = false, error: String? = nil) -> String {
        let errorJSON = error.map { "\"\($0)\"" } ?? "null"
        return """
        {"type":"settings","controls":[{"id":"ai_mode","label":"AI mode","group":"ai","kind":"enum","value":\(value),"min":0,"max":6,"step":1,"default":null,"readable":true,"writable":\(value != 255),"active":true,"unit":"","options":[{"value":0,"label":"Normal"},{"value":1,"label":"Tracking"},{"value":4,"label":"Whiteboard"},{"value":5,"label":"Overhead"},{"value":6,"label":"DeskView"}],"pending":\(pending),"detail":"Camera mode readback","error":\(errorJSON)}]}
        """
    }
}

@MainActor final class AIModeTests: XCTestCase {
    private func waitUntil(_ predicate: @escaping @MainActor () -> Bool) async throws {
        let deadline = ContinuousClock.now.advanced(by: .seconds(3))
        while !predicate() {
            guard ContinuousClock.now < deadline else { XCTFail("Timed out"); throw TestError.timeout }
            try await Task.sleep(for: .milliseconds(5))
        }
    }

    private func connect(_ server: MockDaemon, interval: Duration = .milliseconds(20), timeout: Duration = .seconds(1)) async throws -> AppController {
        let url = try await server.start()
        let controller = AppController(connection: DaemonConnection(url: url), modePollInterval: interval, modeTransitionTimeout: timeout)
        controller.connect(attempts: 1)
        try await waitUntil { controller.aiMode != nil }
        return controller
    }

    func testNonNormalSwitchWaitsForNormalBeforeWritingDeskView() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(1))
        defer { server.stop() }
        let controller = try await connect(server)
        XCTAssertFalse(controller.canJog)
        controller.move(tilt: .negative)
        controller.center()
        XCTAssertFalse(server.snapshot().contains("pan_tilt"))
        XCTAssertFalse(server.snapshot().contains("center"))
        server.scriptSettingsResponses([
            AIModeFixtures.snapshot(255), AIModeFixtures.snapshot(255), AIModeFixtures.snapshot(0),
            AIModeFixtures.snapshot(0), AIModeFixtures.snapshot(255), AIModeFixtures.snapshot(6)
        ])
        controller.setSetting(id: "ai_mode", value: 6)
        XCTAssertTrue(controller.modePending)
        controller.setZoom(200)
        controller.setSetting(id: "ai_mode", value: 5)
        try await waitUntil { !controller.settingsBusy }
        XCTAssertEqual(controller.aiMode?.value, 6)
        XCTAssertTrue(controller.canJog, "DeskView must allow manual tilt after settling")
        XCTAssertFalse(server.snapshot().contains("zoom"))
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") }, ["setting_set:ai_mode=0", "setting_set:ai_mode=6"])
        let events = server.snapshot()
        let target = try XCTUnwrap(events.firstIndex(of: "setting_set:ai_mode=6"))
        XCTAssertEqual(events[..<target].filter { $0 == "settings_get" }.count, 3)
        await controller.shutdown()
    }

    func testInitial255PollsReadOnlyThenStopsAndAllowsExplicitRefresh() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(255))
        defer { server.stop() }
        let controller = try await connect(server, timeout: .milliseconds(90))
        XCTAssertTrue(controller.isSwitchingMode)
        controller.setSetting(id: "ai_mode", value: 0)
        try await waitUntil { controller.settingsError != nil && !controller.settingsBusy }
        XCTAssertEqual(controller.state, .connected)
        XCTAssertTrue(controller.canRefreshSettings)
        XCTAssertFalse(controller.canEditSettings)
        XCTAssertFalse(controller.canJog)
        XCTAssertFalse(server.snapshot().contains("setting_set"))
        let reads = server.snapshot().filter { $0 == "settings_get" }.count
        XCTAssertGreaterThan(reads, 1)
        try await Task.sleep(for: .milliseconds(100))
        XCTAssertEqual(server.snapshot().filter { $0 == "settings_get" }.count, reads)
        server.setSettingsResponse(AIModeFixtures.snapshot(0))
        controller.refreshSettings()
        try await waitUntil { !controller.settingsBusy }
        XCTAssertNil(controller.settingsError)
        XCTAssertTrue(controller.canJog)
        await controller.shutdown()
    }

    func testWhiteboardDetectionPollsUntilLockedAndReportsFailureWithoutDisconnecting() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(0))
        defer { server.stop() }
        let controller = try await connect(server)
        server.scriptSettingsResponses([
            AIModeFixtures.snapshot(4, pending: true), AIModeFixtures.snapshot(4, pending: true), AIModeFixtures.snapshot(4)
        ])
        controller.setSetting(id: "ai_mode", value: 4)
        try await waitUntil { controller.aiMode?.isPending == true }
        XCTAssertFalse(controller.canJog)
        try await waitUntil { !controller.settingsBusy }
        XCTAssertNil(controller.settingsError)
        XCTAssertEqual(controller.aiMode?.value, 4)
        server.setSettingsResponse(AIModeFixtures.snapshot(4, pending: true))
        controller.refreshSettings()
        try await waitUntil { controller.isSwitchingMode }
        server.setSettingsResponse(AIModeFixtures.snapshot(4, error: "Whiteboard detection failed. Place the board in frame."))
        try await waitUntil { !controller.settingsBusy }
        XCTAssertEqual(controller.settingsError, controller.aiMode?.error)
        XCTAssertTrue(controller.settingsError?.contains("Whiteboard detection failed") == true)
        XCTAssertEqual(controller.state, .connected)
        XCTAssertTrue(controller.canRefreshSettings)
        await controller.shutdown()
    }

    func testStopDoesNotWaitForPollingSleepAndDisconnectCancelsSecondWrite() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(1))
        defer { server.stop() }
        let controller = try await connect(server, interval: .seconds(1))
        server.scriptSettingsResponses([AIModeFixtures.snapshot(255)])
        controller.setSetting(id: "ai_mode", value: 6)
        try await waitUntil { controller.aiMode?.value == 255 }
        controller.stopMotion()
        try await waitUntil { server.snapshot().contains("reply:stop") }
        XCTAssertTrue(controller.isSwitchingMode)
        XCTAssertEqual(server.snapshot().filter { $0 == "settings_get" }.count, 1, "Stop runs before the next polling read")
        await controller.disconnect().value
        XCTAssertFalse(controller.isSwitchingMode)
        XCTAssertTrue(controller.settings.isEmpty)
        server.setSettingsResponse(AIModeFixtures.snapshot(0))
        controller.connect(attempts: 1)
        try await waitUntil { controller.aiMode?.value == 0 }
        try await Task.sleep(for: .milliseconds(1100))
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") }, ["setting_set:ai_mode=0"])
        XCTAssertEqual(server.snapshot().filter { $0 == "settings_get" }.count, 2)
        await controller.shutdown()
    }

    func testCameraEpochCancelsInFlightModeReadbackAndDoesNotSendTargetAfterReconnect() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(1), settingsDelay: 0.15)
        defer { server.stop() }
        let controller = try await connect(server)
        server.scriptSettingsResponses([AIModeFixtures.snapshot(0)])
        controller.setSetting(id: "ai_mode", value: 6)
        try await waitUntil { server.snapshot().contains("setting_set:ai_mode=0") }
        server.setCameraConnected(false)
        try await waitUntil { !controller.cameraConnected }
        server.setSettingsResponse(AIModeFixtures.snapshot(5))
        server.setCameraConnected(true)
        try await waitUntil { controller.aiMode?.value == 5 && !controller.settingsBusy }
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") }, ["setting_set:ai_mode=0"])
        XCTAssertNil(controller.settingsError)
        await controller.shutdown()
    }

    func testIgnoredModeWriteTimesOutWithoutResendingOrClaimingSuccess() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(0))
        defer { server.stop() }
        let controller = try await connect(server, timeout: .milliseconds(80))
        server.scriptSettingsResponses([AIModeFixtures.snapshot(0)])
        controller.setSetting(id: "ai_mode", value: 5)
        try await waitUntil { !controller.settingsBusy }
        XCTAssertNotNil(controller.settingsError)
        XCTAssertEqual(controller.aiMode?.value, 0)
        XCTAssertEqual(controller.state, .connected)
        XCTAssertTrue(controller.canRefreshSettings)
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") }, ["setting_set:ai_mode=5"])
        await controller.shutdown()
    }

    func testIgnoredNormalIntermediateNeverSendsFinalMode() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(1))
        defer { server.stop() }
        let controller = try await connect(server, timeout: .milliseconds(80))
        server.scriptSettingsResponses([AIModeFixtures.snapshot(1)])
        controller.setSetting(id: "ai_mode", value: 6)
        try await waitUntil { !controller.settingsBusy }
        XCTAssertNotNil(controller.settingsError)
        XCTAssertEqual(controller.aiMode?.value, 1)
        XCTAssertFalse(controller.canJog)
        XCTAssertTrue(controller.canRefreshSettings)
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") }, ["setting_set:ai_mode=0"])
        await controller.shutdown()
    }

    func testInitialWhiteboardPendingTimesOutAndRefreshCanObserveLock() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(4, pending: true))
        defer { server.stop() }
        let controller = try await connect(server, timeout: .milliseconds(80))
        try await waitUntil { !controller.settingsBusy }
        XCTAssertNotNil(controller.settingsError)
        XCTAssertFalse(controller.canEditSettings)
        XCTAssertFalse(controller.canZoom)
        XCTAssertTrue(controller.canRefreshSettings)
        server.setSettingsResponse(AIModeFixtures.snapshot(4))
        controller.refreshSettings()
        try await waitUntil { !controller.settingsBusy }
        XCTAssertNil(controller.settingsError)
        XCTAssertTrue(controller.canEditSettings)
        XCTAssertFalse(server.snapshot().contains("setting_set"))
        await controller.shutdown()
    }

    private enum TestError: Error { case timeout }
}

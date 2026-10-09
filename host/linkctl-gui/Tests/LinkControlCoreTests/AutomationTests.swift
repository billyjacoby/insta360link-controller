import XCTest
@testable import LinkControlCore
@testable import LinkControlGUI

final class AutomationParserTests: XCTestCase {
    func testEveryDocumentedRouteAndSignedImageValue() throws {
        let cases: [(String, AutomationAction)] = [
            ("center", .center), ("stop", .stop), ("start-preview", .startPreview),
            ("stop-preview", .stopPreview), ("dashboard", .dashboard), ("refresh", .refresh),
            ("zoom?value=100", .zoom(100)), ("zoom?value=400", .zoom(400)),
            ("setting?id=brightness&value=-12", .setting(id: "brightness", value: -12)),
            ("setting?value=0&id=focus_auto", .setting(id: "focus_auto", value: 0))
        ]
        for (route, expected) in cases {
            XCTAssertEqual(try AutomationAction.parse(XCTUnwrap(URL(string: "linkcontrol://action/" + route))), expected)
        }
        for direction in AutomationAction.Jog.allCases {
            XCTAssertEqual(try AutomationAction.parse(URL(string: "linkcontrol://action/jog?direction=\(direction.rawValue)")!), .jog(direction))
        }
        for mode in AutomationAction.Mode.allCases {
            XCTAssertEqual(try AutomationAction.parse(URL(string: "linkcontrol://action/mode?value=\(mode.rawValue)")!), .mode(mode))
        }
    }

    func testRejectsAmbiguousAuthorityRoutesParametersAndNumericSyntax() {
        let invalid = [
            "http://action/center", "linkcontrol://other/center", "linkcontrol:///action/center",
            "linkcontrol://user@action/center", "linkcontrol://user:password@action/center",
            "linkcontrol://action:9001/center", "linkcontrol://action:/center",
            "linkcontrol://action/center#", "linkcontrol://action/center#fragment",
            "linkcontrol://action/center/", "linkcontrol://action//center", "linkcontrol://action/../center",
            "linkcontrol://action/command?cmd=stop", "linkcontrol://action/center?endpoint=ws://localhost:9001",
            "linkcontrol://action/center?", "linkcontrol://action/stop?value=0",
            "linkcontrol://action/jog?direction=up&direction=down", "linkcontrol://action/jog?direction=up&extra=1",
            "linkcontrol://action/jog?direction=UP", "linkcontrol://action/jog?direction", "linkcontrol://action/jog?direction=",
            "linkcontrol://action/zoom", "linkcontrol://action/zoom?value=99", "linkcontrol://action/zoom?value=401",
            "linkcontrol://action/zoom?value=100.0", "linkcontrol://action/zoom?value=1e2",
            "linkcontrol://action/zoom?value=+100", "linkcontrol://action/zoom?value=0100",
            "linkcontrol://action/zoom?value=100&value=200", "linkcontrol://action/zoom?value=100&",
            "linkcontrol://action/zoom?value=%31%30%30", "linkcontrol://action/%63enter",
            "linkcontrol://action/zoom?value=9999999999999999999999999999999999",
            "linkcontrol://action/mode?value=255", "linkcontrol://action/mode?value=unknown",
            "linkcontrol://action/setting?id=ai_mode&value=1", "linkcontrol://action/setting?id=raw_usb&value=1",
            "linkcontrol://action/setting?id=brightness&value=-0", "linkcontrol://action/setting?id=brightness&value=1.2",
            "linkcontrol://action/setting?id=brightness&id=contrast&value=1",
            "linkcontrol://action/setting?id=brightness&value=1&value=2"
        ]
        for text in invalid {
            guard let url = URL(string: text) else { continue }
            XCTAssertThrowsError(try AutomationAction.parse(url), text)
        }
    }
}

@MainActor final class AutomationLifecycleTests: XCTestCase {
    private func waitUntil(_ predicate: @escaping @MainActor () -> Bool) async throws {
        let deadline = ContinuousClock.now.advanced(by: .seconds(4))
        while !predicate() {
            guard ContinuousClock.now < deadline else { XCTFail("Timed out"); throw TestError.timeout }
            try await Task.sleep(for: .milliseconds(5))
        }
    }

    private func connect(_ server: MockDaemon, automationTimeout: Duration = .seconds(2),
                         modePollInterval: Duration = .milliseconds(40), modeTimeout: Duration = .seconds(1)) async throws -> AppController {
        let url = try await server.start()
        let controller = AppController(connection: DaemonConnection(url: url, timeout: .milliseconds(250)),
                                       modePollInterval: modePollInterval, modeTransitionTimeout: modeTimeout,
                                       automationTimeout: automationTimeout)
        controller.connect(attempts: 1)
        try await waitUntil { controller.hasLoadedSettings }
        return controller
    }

    private func fakeBundle() throws -> URL {
        let bundle = FileManager.default.temporaryDirectory.appendingPathComponent("opencode/Automation-\(UUID()).app")
        let helper = bundle.appendingPathComponent("Contents/Helpers/linkctl-daemon")
        try FileManager.default.createDirectory(at: helper.deletingLastPathComponent(), withIntermediateDirectories: true)
        let script = """
        #!/bin/sh
        trap 'exit 0' TERM
        /bin/sleep 0.15
        printf '[main] linkctl-daemon running on port %s\\n' "$2"
        while :; do /bin/sleep 0.05; done
        """
        try Data(script.utf8).write(to: helper)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: helper.path)
        return bundle
    }

    func testColdLaunchWaitsForOwnedHelperAndSettingsThenExecutesInOrder() async throws {
        let server = try MockDaemon(settingsDelay: 0.12)
        let url = try await server.start()
        defer { server.stop() }
        let bundle = try fakeBundle()
        defer { try? FileManager.default.removeItem(at: bundle) }
        var launches = 0
        let controller = AppController(bundleURL: bundle, portProvider: { launches += 1; return UInt16(url.port!) })
        controller.applicationDidLaunch()
        controller.submitAutomation(.setting(id: "brightness", value: 8))
        controller.submitAutomation(.zoom(200))
        controller.submitAutomation(.jog(.up))
        controller.submitAutomation(.center)
        try await Task.sleep(for: .milliseconds(80))
        XCTAssertTrue(server.snapshot().isEmpty)
        try await waitUntil { controller.automationMessage == "Acknowledged: Center." }
        XCTAssertEqual(launches, 1)
        XCTAssertEqual(server.commandSnapshot().compactMap { $0["cmd"] as? String },
                       ["status", "settings_get", "setting_set", "zoom", "pan_tilt", "stop", "center", "stop"])
        let jog = try XCTUnwrap(server.commandSnapshot().first { $0["cmd"] as? String == "pan_tilt" })
        XCTAssertEqual(jog["tilt_dir"] as? Int, 255, "Up preserves inverted Y")
        XCTAssertFalse(controller.automationFailed)
        await controller.shutdown()
    }

    func testStopCancelsAllColdStartupMovesWithoutDelayedReplay() async throws {
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let bundle = try fakeBundle()
        defer { try? FileManager.default.removeItem(at: bundle) }
        let controller = AppController(bundleURL: bundle, portProvider: { UInt16(url.port!) })
        controller.applicationDidLaunch()
        controller.submitAutomation(.jog(.up))
        controller.submitAutomation(.center)
        controller.submitAutomation(.zoom(300))
        try await Task.sleep(for: .milliseconds(20))
        controller.submitAutomation(.stop)
        XCTAssertTrue(controller.automationMessage.contains("cancelled"))
        try await waitUntil { controller.hasLoadedSettings }
        try await Task.sleep(for: .milliseconds(100))
        XCTAssertFalse(server.snapshot().contains("pan_tilt"))
        XCTAssertFalse(server.snapshot().contains("center"))
        XCTAssertFalse(server.snapshot().contains("zoom"))
        await controller.shutdown()
    }

    func testMissingCameraWaitIsBoundedAndReportsFailure() async throws {
        let server = try MockDaemon(cameraConnected: false)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url), automationTimeout: .milliseconds(80))
        controller.connect(attempts: 1)
        controller.submitAutomation(.center)
        try await waitUntil { controller.automationFailed }
        XCTAssertTrue(controller.automationMessage.contains("Timed out"))
        XCTAssertFalse(server.snapshot().contains("center"))
        await controller.shutdown()
    }

    func testQueueCapacityIsBounded() async throws {
        let controller = AppController(automationTimeout: .seconds(1))
        for _ in 0..<33 { controller.submitAutomation(.jog(.left)) }
        XCTAssertTrue(controller.automationFailed)
        XCTAssertTrue(controller.automationMessage.contains("queue is full"))
        await controller.shutdown()
    }

    func testBackendAcknowledgementIsRequiredBeforeSuccess() async throws {
        let server = try MockDaemon(ackDelay: 0.1)
        defer { server.stop() }
        let controller = try await connect(server)
        controller.submitAutomation(.zoom(150))
        try await waitUntil { server.snapshot().contains("zoom") }
        XCTAssertFalse(controller.automationMessage.hasPrefix("Acknowledged"))
        try await waitUntil { controller.automationMessage == "Acknowledged: Zoom 150%." }
        await controller.shutdown()
    }

    func testCommandTimeoutIsVisibleAndNeverClaimsSuccess() async throws {
        let server = try MockDaemon(acknowledgeCommands: false)
        defer { server.stop() }
        let controller = try await connect(server)
        controller.submitAutomation(.zoom(200))
        try await waitUntil { controller.automationFailed }
        XCTAssertFalse(controller.automationMessage.hasPrefix("Acknowledged"))
        XCTAssertTrue(controller.automationMessage.contains("failed"))
        await controller.shutdown()
    }

    func testSettingCapabilitiesRejectUnavailableInactiveAndOutOfRangeWrites() async throws {
        let server = try MockDaemon()
        defer { server.stop() }
        let controller = try await connect(server)
        for action in [AutomationAction.setting(id: "brightness", value: 65), .setting(id: "focus_absolute", value: 80),
                       .setting(id: "contrast", value: 1), .setting(id: "ai_mode", value: 1)] {
            controller.submitAutomation(action)
            try await waitUntil { controller.automationFailed }
        }
        XCTAssertFalse(server.snapshot().contains("setting_set"))
        controller.submitAutomation(.setting(id: "brightness", value: -4))
        try await waitUntil { controller.automationMessage == "Acknowledged: brightness = -4." }
        XCTAssertEqual(controller.settings.first { $0.id == "brightness" }?.value, -4)
        await controller.shutdown()
    }

    func testSettingErrorPropagatesAndExplicitRefreshRecovers() async throws {
        let server = try MockDaemon()
        defer { server.stop() }
        let controller = try await connect(server)
        server.setSettingsResponse(#"{"type":"settings_error","message":"USB write failed"}"#)
        controller.submitAutomation(.setting(id: "brightness", value: 2))
        try await waitUntil { controller.automationFailed }
        XCTAssertTrue(controller.automationMessage.contains("USB write failed"))
        server.setSettingsResponse(MockDaemon.defaultSettings)
        controller.submitAutomation(.refresh)
        try await waitUntil { controller.automationMessage == "Acknowledged: Refresh." }
        XCTAssertNil(controller.settingsError)
        await controller.shutdown()
    }

    func testFailedWriteAllowsNextValidWriteAndManualActionWithoutRefresh() async throws {
        let server = try MockDaemon()
        defer { server.stop() }
        let controller = try await connect(server)
        server.setSettingsResponse(#"{"type":"settings_error","message":"USB write failed"}"#)
        controller.submitAutomation(.setting(id: "brightness", value: 2))
        try await waitUntil { controller.automationFailed }
        XCTAssertTrue(controller.hasLoadedSettings)
        XCTAssertTrue(controller.automationCameraReady)
        controller.submitAutomation(.zoom(150))
        try await waitUntil { controller.automationMessage == "Acknowledged: Zoom 150%." }
        server.setSettingsResponse(MockDaemon.defaultSettings)
        controller.submitAutomation(.setting(id: "brightness", value: 3))
        try await waitUntil { controller.automationMessage == "Acknowledged: brightness = 3." }
        XCTAssertNil(controller.settingsError)
        XCTAssertEqual(server.snapshot().filter { $0 == "settings_get" }.count, 1)
        await controller.shutdown()
    }

    func testFailedWhiteboardRecoversToNormalWithoutRefresh() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(0))
        defer { server.stop() }
        let controller = try await connect(server)
        server.scriptSettingsResponses([AIModeFixtures.snapshot(4, error: "Whiteboard detection failed")])
        controller.submitAutomation(.mode(.whiteboard))
        try await waitUntil { controller.automationFailed }
        XCTAssertEqual(controller.aiMode?.value, 4)
        XCTAssertNotNil(controller.settingsError)
        XCTAssertTrue(controller.automationCameraReady)
        server.scriptSettingsResponses([AIModeFixtures.snapshot(0)])
        controller.submitAutomation(.mode(.normal))
        try await waitUntil { controller.automationMessage == "Acknowledged: Mode normal." }
        XCTAssertNil(controller.settingsError)
        XCTAssertEqual(controller.aiMode?.value, 0)
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") }, ["setting_set:ai_mode=4", "setting_set:ai_mode=0"])
        XCTAssertEqual(server.snapshot().filter { $0 == "settings_get" }.count, 1)
        await controller.shutdown()
    }

    func testSameModeNoOpClearsUnrelatedPriorSettingsError() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(0))
        defer { server.stop() }
        let controller = try await connect(server)
        server.setSettingsResponse(#"{"type":"settings_error","message":"Transient settings read failed"}"#)
        controller.submitAutomation(.refresh)
        try await waitUntil { controller.automationFailed }
        XCTAssertNotNil(controller.settingsError)
        controller.submitAutomation(.mode(.normal))
        try await waitUntil { controller.automationMessage == "Acknowledged: Mode normal." }
        XCTAssertNil(controller.settingsError)
        XCTAssertFalse(server.snapshot().contains("setting_set"))
        await controller.shutdown()
    }

    func testInitialDiscoveryFailureStillRequiresExplicitRefresh() async throws {
        let server = try MockDaemon(settingsReply: #"{"type":"settings_error","message":"Initial read failed"}"#)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.settingsError != nil }
        XCTAssertFalse(controller.hasLoadedSettings)
        XCTAssertFalse(controller.automationCameraReady)
        controller.submitAutomation(.zoom(200))
        try await waitUntil { controller.automationFailed }
        XCTAssertTrue(controller.automationMessage.contains("Initial read failed"))
        XCTAssertFalse(server.snapshot().contains("zoom"))
        server.setSettingsResponse(MockDaemon.defaultSettings)
        controller.submitAutomation(.refresh)
        try await waitUntil { controller.automationMessage == "Acknowledged: Refresh." }
        XCTAssertTrue(controller.automationCameraReady)
        await controller.shutdown()
    }

    func testModeSuccessAndTrackingGatesManualMotion() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(0))
        defer { server.stop() }
        let controller = try await connect(server)
        controller.submitAutomation(.mode(.tracking))
        try await waitUntil { controller.automationMessage == "Acknowledged: Mode tracking." }
        XCTAssertEqual(controller.aiMode?.value, 1)
        controller.submitAutomation(.jog(.up))
        try await waitUntil { controller.automationFailed }
        XCTAssertFalse(server.snapshot().contains("pan_tilt"))
        controller.submitAutomation(.mode(.deskview))
        try await waitUntil { controller.automationMessage == "Acknowledged: Mode deskview." }
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") },
                       ["setting_set:ai_mode=1", "setting_set:ai_mode=0", "setting_set:ai_mode=6"])
        await controller.shutdown()
    }

    func testStopOvertakesModeWaitAndCancelsQueuedMoves() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(0))
        defer { server.stop() }
        let controller = try await connect(server, modePollInterval: .milliseconds(500))
        server.scriptSettingsResponses([AIModeFixtures.snapshot(255)])
        controller.submitAutomation(.mode(.overhead))
        controller.submitAutomation(.jog(.up))
        controller.submitAutomation(.zoom(200))
        try await waitUntil { controller.aiMode?.value == 255 }
        controller.submitAutomation(.stop)
        try await waitUntil { controller.automationMessage.hasPrefix("Acknowledged: Stop") }
        XCTAssertTrue(controller.isSwitchingMode)
        XCTAssertLessThanOrEqual(server.snapshot().filter { $0 == "settings_get" }.count, 2,
                                 "Only initial discovery and cancellation readback may precede the next poll")
        server.setSettingsResponse(AIModeFixtures.snapshot(5))
        try await waitUntil { !controller.isSwitchingMode }
        try await Task.sleep(for: .milliseconds(50))
        XCTAssertFalse(server.snapshot().contains("pan_tilt"))
        XCTAssertFalse(server.snapshot().contains("zoom"))
        await controller.shutdown()
    }

    func testModeFailureNeverClaimsSuccessAndDoesNotReleaseQueuedJog() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(0))
        defer { server.stop() }
        let controller = try await connect(server, modeTimeout: .milliseconds(80))
        server.scriptSettingsResponses([AIModeFixtures.snapshot(255)])
        controller.submitAutomation(.mode(.whiteboard))
        controller.submitAutomation(.jog(.up))
        try await waitUntil { controller.automationFailed && !controller.settingsBusy }
        try await Task.sleep(for: .milliseconds(40))
        XCTAssertFalse(server.snapshot().contains("pan_tilt"))
        XCTAssertFalse(controller.logs.contains("Acknowledged: Mode whiteboard"))
        await controller.shutdown()
    }

    func testStopDuringExternalIntermediateNormalCancelsFinalModeAndQueuedSettings() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(6))
        defer { server.stop() }
        let controller = try await connect(server, modePollInterval: .milliseconds(200))
        server.scriptSettingsResponses([AIModeFixtures.snapshot(255)])
        controller.submitAutomation(.mode(.overhead))
        controller.submitAutomation(.setting(id: "brightness", value: 2))
        controller.submitAutomation(.jog(.up))
        try await waitUntil { controller.aiMode?.value == 255 }
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") }, ["setting_set:ai_mode=0"])
        server.setSettingsResponse(AIModeFixtures.snapshot(0))
        controller.submitAutomation(.stop)
        try await waitUntil { controller.automationMessage.hasPrefix("Acknowledged: Stop") && controller.hasLoadedSettings }
        try await Task.sleep(for: .milliseconds(250))
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") }, ["setting_set:ai_mode=0"])
        XCTAssertFalse(server.snapshot().contains("pan_tilt"))
        XCTAssertEqual(controller.aiMode?.value, 0)
        XCTAssertFalse(controller.logs.contains("Acknowledged: Mode overhead"))
        await controller.shutdown()
    }

    func testExternalStopDoesNotCancelUnrelatedDashboardModeOperation() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(6))
        defer { server.stop() }
        let controller = try await connect(server, modePollInterval: .milliseconds(150))
        server.scriptSettingsResponses([AIModeFixtures.snapshot(255)])
        controller.setSetting(id: "ai_mode", value: 5)
        controller.submitAutomation(.jog(.up))
        try await waitUntil { controller.aiMode?.value == 255 }
        controller.submitAutomation(.stop)
        try await waitUntil { server.snapshot().contains("reply:stop") }
        XCTAssertTrue(controller.isSwitchingMode)
        server.setSettingsResponse(AIModeFixtures.snapshot(0))
        try await waitUntil { controller.aiMode?.value == 5 && !controller.isSwitchingMode }
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") }, ["setting_set:ai_mode=0", "setting_set:ai_mode=5"])
        XCTAssertFalse(server.snapshot().contains("pan_tilt"))
        await controller.shutdown()
    }

    func testRepeatedStopCannotCancelTheOnlyReservedStopRequest() async throws {
        let server = try MockDaemon(ackDelay: 0.08)
        defer { server.stop() }
        let controller = try await connect(server)
        controller.submitAutomation(.stop)
        controller.submitAutomation(.stop)
        controller.submitAutomation(.stop)
        try await waitUntil { controller.automationMessage.hasPrefix("Acknowledged: Stop") }
        XCTAssertEqual(server.snapshot().filter { $0 == "stop" }.count, 1)
        await controller.shutdown()
    }

    func testStopCancelsExternalSettingReservedBehindAnInFlightCommand() async throws {
        let server = try MockDaemon(ackDelay: 0.12)
        defer { server.stop() }
        let controller = try await connect(server)
        controller.setZoom(150)
        try await waitUntil { server.snapshot().contains("zoom") }
        controller.submitAutomation(.setting(id: "brightness", value: 8))
        try await waitUntil { controller.savingSettingID == "brightness" }
        controller.submitAutomation(.stop)
        try await waitUntil { controller.automationMessage.hasPrefix("Acknowledged: Stop") && controller.hasLoadedSettings }
        XCTAssertFalse(server.snapshot().contains("setting_set"))
        XCTAssertEqual(controller.settings.first { $0.id == "brightness" }?.value, 0)
        await controller.shutdown()
    }

    func testReadinessDeadlineDoesNotMisreportACompletedLongerModeTransition() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(6))
        defer { server.stop() }
        let controller = try await connect(server, automationTimeout: .milliseconds(100),
                                           modePollInterval: .milliseconds(80))
        server.scriptSettingsResponses([
            AIModeFixtures.snapshot(255), AIModeFixtures.snapshot(0),
            AIModeFixtures.snapshot(255), AIModeFixtures.snapshot(5)
        ])
        controller.submitAutomation(.mode(.overhead))
        try await waitUntil { controller.automationMessage == "Acknowledged: Mode overhead." }
        XCTAssertEqual(controller.aiMode?.value, 5)
        XCTAssertFalse(controller.automationFailed)
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") },
                       ["setting_set:ai_mode=0", "setting_set:ai_mode=5"])
        await controller.shutdown()
    }

    func testInternalStopCancelsOldExternalJogWithoutClobberingNewDashboardJog() async throws {
        let server = try MockDaemon()
        defer { server.stop() }
        let controller = try await connect(server)
        controller.submitAutomation(.jog(.up))
        controller.submitAutomation(.center)
        controller.submitAutomation(.zoom(200))
        try await waitUntil { server.snapshot().contains("pan_tilt") }
        controller.stopMotion()
        controller.move(pan: .positive)
        XCTAssertTrue(controller.motionActive)
        try await waitUntil { server.snapshot().filter { $0 == "pan_tilt" }.count == 2 }
        try await Task.sleep(for: .milliseconds(80))
        XCTAssertTrue(controller.motionActive, "Cancelled external jog must not clear a newer dashboard jog's state")
        XCTAssertEqual(server.snapshot().filter { $0 == "stop" }.count, 1,
                       "Only internal Stop, not the cancelled jog's final Stop, may run before the new jog deadline")
        try await waitUntil { !controller.motionActive }
        try await waitUntil { server.snapshot().filter { $0 == "stop" }.count == 2 }
        XCTAssertFalse(server.snapshot().contains("center"))
        XCTAssertFalse(server.snapshot().contains("zoom"))
        XCTAssertFalse(controller.logs.contains("Acknowledged: Jog up."))
        await controller.shutdown()
    }

    func testInternalStopPreservesExternalModeOperationButCancelsQueuedManualActions() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(6))
        defer { server.stop() }
        let controller = try await connect(server, modePollInterval: .milliseconds(100))
        server.scriptSettingsResponses([AIModeFixtures.snapshot(255)])
        controller.submitAutomation(.mode(.overhead))
        controller.submitAutomation(.jog(.up))
        try await waitUntil { controller.aiMode?.value == 255 }
        controller.stopMotion()
        try await waitUntil { server.snapshot().contains("reply:stop") }
        server.setSettingsResponse(AIModeFixtures.snapshot(0))
        try await waitUntil { controller.automationMessage == "Acknowledged: Mode overhead." }
        XCTAssertEqual(controller.aiMode?.value, 5)
        XCTAssertFalse(server.snapshot().contains("pan_tilt"))
        await controller.shutdown()
    }

    func testInternalStopPreservesAnExternalSettingWrite() async throws {
        let server = try MockDaemon(ackDelay: 0.08)
        defer { server.stop() }
        let controller = try await connect(server)
        controller.setZoom(150)
        try await waitUntil { server.snapshot().contains("zoom") }
        controller.submitAutomation(.setting(id: "brightness", value: 8))
        try await waitUntil { controller.savingSettingID == "brightness" }
        controller.stopMotion()
        try await waitUntil { controller.automationMessage == "Acknowledged: brightness = 8." }
        XCTAssertEqual(controller.settings.first { $0.id == "brightness" }?.value, 8)
        await controller.shutdown()
    }

    func testDisconnectAndCameraReconnectCancelQueuedActions() async throws {
        let server = try MockDaemon(settingsReply: AIModeFixtures.snapshot(0))
        defer { server.stop() }
        let controller = try await connect(server, modePollInterval: .milliseconds(500))
        server.scriptSettingsResponses([AIModeFixtures.snapshot(255)])
        controller.submitAutomation(.mode(.deskview))
        controller.submitAutomation(.center)
        try await waitUntil { controller.aiMode?.value == 255 }
        server.setCameraConnected(false)
        try await waitUntil { !controller.cameraConnected }
        XCTAssertTrue(controller.automationFailed)
        server.setSettingsResponse(AIModeFixtures.snapshot(0))
        server.setCameraConnected(true)
        try await waitUntil { controller.hasLoadedSettings }
        try await Task.sleep(for: .milliseconds(100))
        XCTAssertFalse(server.snapshot().contains("center"))
        await controller.shutdown()
    }

    func testShutdownCancelsStartupQueueAndRejectsFurtherActions() async throws {
        let server = try MockDaemon(cameraConnected: false)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        controller.submitAutomation(.jog(.right))
        try await waitUntil { controller.state == .connected }
        await controller.shutdown()
        controller.submitAutomation(.center)
        XCTAssertTrue(controller.automationMessage.contains("shutting down"))
        server.setCameraConnected(true)
        try await Task.sleep(for: .milliseconds(80))
        XCTAssertFalse(server.snapshot().contains("pan_tilt"))
        XCTAssertFalse(server.snapshot().contains("center"))
    }

    private enum TestError: Error { case timeout }
}

import XCTest
@testable import LinkControlCore
@testable import LinkControlGUI

enum GestureSettingsFixtures {
    static let ids = ["gesture_control", "gesture_palm", "gesture_zoom", "gesture_whiteboard"]

    static func snapshot(value: Int = 1, readable: Bool = true, writable: Bool = true) -> String {
        let controls = ids.map { id in
            """
            {"id":"\(id)","label":"\(id)","group":"gestures","kind":"boolean","value":\(readable ? String(value) : "null"),"min":0,"max":1,"step":1,"default":null,"readable":\(readable),"writable":\(writable),"active":true,"unit":"","options":[],"error":null}
            """
        }.joined(separator: ",")
        return "{\"type\":\"settings\",\"controls\":[\(controls)]}"
    }
}

final class GestureSettingsTests: XCTestCase {
    func testGestureControlsDecodeAsIndependentBooleansWithoutInventedDefaults() throws {
        guard case let .settings(controls) = try DaemonReply.decode(Data(GestureSettingsFixtures.snapshot().utf8)) else {
            return XCTFail("Expected gesture settings")
        }
        XCTAssertEqual(controls.map(\.id), GestureSettingsFixtures.ids)
        for control in controls {
            XCTAssertEqual(control.group, .gestures)
            XCTAssertEqual(control.kind, .boolean)
            XCTAssertTrue(control.canEdit)
            XCTAssertFalse(control.canReset)
            XCTAssertEqual(control.formattedValue(control.value), "On")
            XCTAssertTrue(control.accepts(0))
            XCTAssertTrue(control.accepts(1))
            XCTAssertFalse(control.accepts(2))
            XCTAssertEqual(try DaemonCommand.settingSet(id: control.id, value: 0).jsonString(),
                           "{\"cmd\":\"setting_set\",\"id\":\"\(control.id)\",\"value\":0}")
        }
    }

    func testUnavailableGestureStateIsNotDisplayedAsOff() throws {
        guard case let .settings(controls) = try DaemonReply.decode(Data(GestureSettingsFixtures.snapshot(readable: false, writable: false).utf8)) else {
            return XCTFail("Expected unavailable gesture settings")
        }
        for control in controls {
            XCTAssertNil(control.value)
            XCTAssertEqual(control.formattedValue(control.value), "Unavailable")
            XCTAssertFalse(control.canEdit)
        }
        guard case .settingsError = try DaemonReply.decode(Data(GestureSettingsFixtures.snapshot(value: 2).utf8)) else {
            return XCTFail("An invalid editable boolean must be rejected")
        }
    }
}

@MainActor final class GestureSettingsLifecycleTests: XCTestCase {
    private enum TestError: Error { case timeout }

    private func waitUntil(_ predicate: @escaping @MainActor () -> Bool) async throws {
        let deadline = ContinuousClock.now.advanced(by: .seconds(3))
        while !predicate() {
            guard ContinuousClock.now < deadline else { XCTFail("Timed out"); throw TestError.timeout }
            try await Task.sleep(for: .milliseconds(10))
        }
    }

    func testMasterOffPreservesIndividualOptionsAndFailedWriteDoesNotChangeState() async throws {
        let server = try MockDaemon(settingsReply: GestureSettingsFixtures.snapshot())
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.settings.count == 4 && !controller.settingsBusy }
        controller.setSetting(id: "gesture_control", value: 0)
        try await waitUntil { !controller.settingsBusy }
        XCTAssertEqual(controller.settings.first?.value, 0)
        XCTAssertTrue(controller.settings.dropFirst().allSatisfy { $0.value == 1 })
        server.setSettingsResponse(#"{"type":"settings_error","message":"Camera did not apply gesture change"}"#)
        controller.setSetting(id: "gesture_control", value: 1)
        try await waitUntil { controller.settingsError != nil && !controller.settingsBusy }
        XCTAssertEqual(controller.settings.first?.value, 0)
        XCTAssertEqual(controller.state, .connected)
        XCTAssertTrue(controller.canRefreshSettings)
        await controller.shutdown()
    }

    func testReconnectReadsGestureStateWithoutReapplyingPreviousPreferences() async throws {
        let server = try MockDaemon(settingsReply: GestureSettingsFixtures.snapshot())
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.settings.count == 4 && !controller.settingsBusy }
        controller.setSetting(id: "gesture_control", value: 0)
        try await waitUntil { !controller.settingsBusy }
        XCTAssertEqual(controller.settings.first?.value, 0)
        await controller.disconnect().value
        server.setSettingsResponse(GestureSettingsFixtures.snapshot())
        controller.connect(attempts: 1)
        try await waitUntil { controller.settings.count == 4 && !controller.settingsBusy }
        XCTAssertEqual(controller.settings.first?.value, 1, "Use the newly connected camera's state")
        XCTAssertEqual(server.snapshot().filter { $0.hasPrefix("setting_set:") }, ["setting_set:gesture_control=0"])
        await controller.shutdown()
    }
}

import XCTest
@testable import LinkControlGUI

/// Run after packaging with LINKCTL_TEST_BUNDLE pointing at a complete .app.
/// Reads status/settings and sends the normal stop during shutdown; never edits settings or jogs/zooms.
@MainActor final class PackagedAppTests: XCTestCase {
    func testBundledServiceStartsAutomaticallyAndShutsDown() async throws {
        guard let path = ProcessInfo.processInfo.environment["LINKCTL_TEST_BUNDLE"] else {
            throw XCTSkip("Set LINKCTL_TEST_BUNDLE to run the real packaged-helper check")
        }
        let controller = AppController(bundleURL: URL(fileURLWithPath: path))
        controller.applicationDidLaunch()
        do {
            let deadline = Date().addingTimeInterval(20)
            while controller.state != .connected && Date() < deadline {
                if case .error = controller.state { break }
                try await Task.sleep(for: .milliseconds(50))
            }
            XCTAssertEqual(controller.state, .connected, controller.logs)
            XCTAssertTrue(controller.isDaemonRunning, controller.logs)
            XCTAssertTrue(controller.logs.contains("--auto-camera"), controller.logs)
            XCTAssertTrue(controller.logs.contains("--no-control"), controller.logs)
            if controller.cameraConnected {
                let settingsDeadline = Date().addingTimeInterval(15)
                while controller.isLoadingSettings && Date() < settingsDeadline {
                    try await Task.sleep(for: .milliseconds(50))
                }
                XCTAssertFalse(controller.isLoadingSettings, controller.logs)
                XCTAssertNil(controller.settingsError, controller.settingsError ?? "")
                XCTAssertFalse(controller.settings.isEmpty, "Connected camera did not expose image settings")
                XCTAssertEqual(controller.state, .connected, controller.logs)
            }
            await controller.shutdown()
            XCTAssertFalse(controller.isDaemonRunning, controller.logs)
            XCTAssertEqual(controller.state, .offline, controller.logs)
        } catch {
            await controller.shutdown()
            throw error
        }
    }
}

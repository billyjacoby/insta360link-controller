import XCTest
@testable import LinkControlCore
@testable import LinkControlGUI

@MainActor final class LifecycleTests: XCTestCase {
    private func waitUntil(
        timeout: TimeInterval = 3,
        _ predicate: @escaping @MainActor () -> Bool
    ) async throws {
        let deadline = Date().addingTimeInterval(timeout)
        while !predicate() {
            if Date() > deadline {
                XCTFail("Condition did not become true within \(timeout) seconds")
                throw TestError.timeout
            }
            try await Task.sleep(for: .milliseconds(10))
        }
    }

    func testFIFOIncludesSuspendedCommandsAndFinalStop() async {
        let queue = CommandQueue()
        var events: [String] = []
        queue.submit {
            events.append("move-start")
            try? await Task.sleep(for: .milliseconds(30))
            events.append("move-finish")
        }
        queue.submit { events.append("stop") }
        queue.submit { events.append("disconnect") }
        await queue.drain()
        XCTAssertEqual(events, ["move-start", "move-finish", "stop", "disconnect"])
    }

    func testHandshakeAloneDoesNotConnectAndTimesOut() async throws {
        let server = try MockDaemon(mode: .silent)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url, timeout: .milliseconds(250)))
        controller.connect(attempts: 1)
        try await waitUntil { server.snapshot().contains("status") }
        XCTAssertEqual(controller.state, .connecting)
        XCTAssertFalse(controller.controlsEnabled)
        try await waitUntil { if case .error = controller.state { return true }; return false }
        XCTAssertFalse(controller.cameraConnected)
        await controller.shutdown()
    }

    func testMalformedStatusFailsConnection() async throws {
        let server = try MockDaemon(mode: .malformed)
        let url = try await server.start()
        defer { server.stop() }
        let connection = DaemonConnection(url: url)
        do {
            _ = try await connection.connect()
            XCTFail("Malformed status must not connect")
        } catch {
            XCTAssertEqual(error as? DaemonReply.ProtocolError, .malformedReply)
        }
        await connection.disconnect()
    }

    func testStartupRetriesUntilStatusAndUsesTextFrames() async throws {
        let server = try MockDaemon(mode: .warmingUp(2))
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 3)
        try await waitUntil { controller.state == .connected }
        XCTAssertTrue(controller.controlsEnabled)
        XCTAssertEqual(controller.reportedZoom, 125)
        XCTAssertEqual(server.snapshot().filter { $0 == "status" }.count, 3)
        XCTAssertFalse(server.snapshot().contains("non-text"))
        await controller.shutdown()
    }

    func testDisconnectWaitsForStopAckAndCancelsCenterDeadline() async throws {
        let server = try MockDaemon(ackDelay: 0.12)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.canJog }
        controller.center()
        try await waitUntil { server.snapshot().contains("center") }
        let closed = controller.disconnect()
        XCTAssertEqual(controller.state, .disconnecting)
        XCTAssertFalse(controller.cameraConnected)
        controller.move(pan: .positive)
        await closed.value
        XCTAssertEqual(controller.state, .offline)
        XCTAssertTrue(server.snapshot().contains("reply:stop"))
        XCTAssertFalse(server.snapshot().contains("pan_tilt"))
        controller.connect(attempts: 1)
        try await waitUntil { controller.controlsEnabled }
        try await Task.sleep(for: .milliseconds(650))
        XCTAssertEqual(server.snapshot().filter { $0 == "stop" }.count, 1,
                       "An old center timer must not stop a new connection")
        await controller.shutdown()
    }

    func testConnectionLossClearsCameraAndReconnectIgnoresOldCallbacks() async throws {
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.controlsEnabled }
        server.dropClients()
        try await waitUntil { if case .error = controller.state { return true }; return false }
        XCTAssertFalse(controller.cameraConnected)
        controller.connect(attempts: 1)
        try await waitUntil { controller.controlsEnabled }
        try await Task.sleep(for: .milliseconds(100))
        XCTAssertEqual(controller.state, .connected)
        await controller.shutdown()
    }

    func testCameraUnavailableRejectsAllMovementAndZoom() async throws {
        let server = try MockDaemon(cameraConnected: false)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.state == .connected }
        controller.move(pan: .positive)
        controller.center()
        controller.setZoom(200)
        try await Task.sleep(for: .milliseconds(80))
        let events = server.snapshot()
        XCTAssertFalse(events.contains("pan_tilt"))
        XCTAssertFalse(events.contains("center"))
        XCTAssertFalse(events.contains("zoom"))
        await controller.shutdown()
    }

    func testJogAutomaticallyStopsAndDoesNotExtendOnRepeatedClicks() async throws {
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url))
        controller.connect(attempts: 1)
        try await waitUntil { controller.canJog }
        controller.move(pan: .positive)
        controller.move(pan: .negative)
        try await waitUntil { server.snapshot().contains("stop") }
        XCTAssertEqual(server.snapshot().filter { $0 == "pan_tilt" }.count, 1)
        XCTAssertFalse(controller.motionActive)
        await controller.shutdown()
    }

    func testLogsRetainOnlyNewestBoundedCharacters() {
        let buffer = BoundedLogBuffer(capacity: 8)
        buffer.append("abcdefgh")
        buffer.append("ijk")
        XCTAssertEqual(buffer.snapshot(), "defghijk")
        buffer.append("0123456789")
        XCTAssertEqual(buffer.snapshot(), "23456789")
        buffer.clear()
        XCTAssertEqual(buffer.snapshot(), "")
    }

    func testShutdownIsBoundedWhenStopIsNotAcknowledged() async throws {
        let server = try MockDaemon(acknowledgeCommands: false)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url, timeout: .milliseconds(200)))
        controller.connect(attempts: 1)
        try await waitUntil { controller.controlsEnabled }
        let start = Date()
        await controller.shutdown()
        XCTAssertLessThan(Date().timeIntervalSince(start), 1)
        XCTAssertEqual(controller.state, .offline)
        XCTAssertTrue(controller.logs.contains("Final stop could not be sent"))
        XCTAssertTrue(server.snapshot().contains("stop"))
    }

    func testDisconnectDuringStartupCannotBecomeConnectedLater() async throws {
        let server = try MockDaemon(mode: .silent)
        let url = try await server.start()
        defer { server.stop() }
        let controller = AppController(connection: DaemonConnection(url: url, timeout: .milliseconds(200)))
        controller.connect(attempts: 3)
        try await waitUntil { server.snapshot().contains("status") }
        await controller.disconnect().value
        try await Task.sleep(for: .milliseconds(500))
        XCTAssertEqual(controller.state, .offline)
        XCTAssertFalse(controller.cameraConnected)
        XCTAssertEqual(server.snapshot().filter { $0 == "status" }.count, 1)
        await controller.shutdown()
    }

    private func makeBundle(script: String = """
        #!/bin/sh
        printf 'child-args:%s\\n' "$*"
        trap 'exit 0' TERM
        printf '[main] linkctl-daemon running on port %s\\n' "$2"
        while :; do /bin/sleep 0.1; done
        """) throws -> URL {
        let bundle = FileManager.default.temporaryDirectory
            .appendingPathComponent("opencode/LinkControl-tests-\(UUID().uuidString).app")
        let executable = bundle.appendingPathComponent("Contents/Helpers/linkctl-daemon")
        try FileManager.default.createDirectory(at: executable.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data(script.utf8).write(to: executable)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: executable.path)
        return bundle
    }

    func testAutomaticStartupUsesBundledHelperOnceAndPrivateEndpointFlags() async throws {
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let bundle = try makeBundle()
        defer { try? FileManager.default.removeItem(at: bundle) }
        let port = UInt16(url.port!)
        var allocations = 0
        let controller = AppController(bundleURL: bundle, portProvider: { allocations += 1; return port })
        controller.applicationDidLaunch()
        controller.applicationDidLaunch()
        controller.retryConnection() // Repeated clicks during startup must not spawn children.
        XCTAssertEqual(controller.statusLabel, "Connecting camera…")
        try await waitUntil { controller.controlsEnabled }
        XCTAssertTrue(controller.logs.contains("child-args:--port \(port) --no-control --loopback --auto-camera --no-mdns"))
        XCTAssertEqual(allocations, 1)
        XCTAssertEqual(controller.statusLabel, "Camera ready")
        XCTAssertTrue(controller.isDaemonRunning)
        controller.applicationDidLaunch()
        XCTAssertEqual(allocations, 1)
        await controller.shutdown()
        XCTAssertFalse(controller.isDaemonRunning)
        XCTAssertEqual(controller.state, .offline)
        XCTAssertTrue(server.snapshot().contains("reply:stop"))
        // The unrelated in-process server was never terminated by the app.
        let otherConnection = DaemonConnection(url: url)
        _ = try await otherConnection.connect()
        await otherConnection.disconnect()
    }

    func testMissingBundledHelperExplainsReinstallAndCanRetryAfterRepair() async throws {
        let bundle = try makeBundle()
        defer { try? FileManager.default.removeItem(at: bundle) }
        let executable = bundle.appendingPathComponent("Contents/Helpers/linkctl-daemon")
        try FileManager.default.setAttributes([.posixPermissions: 0o644], ofItemAtPath: executable.path)
        let server = try MockDaemon(cameraConnected: false)
        let url = try await server.start()
        defer { server.stop() }
        var allocations = 0
        let controller = AppController(bundleURL: bundle, portProvider: { allocations += 1; return UInt16(url.port!) })
        controller.applicationDidLaunch()
        try await waitUntil { controller.canRetryConnection }
        XCTAssertTrue(controller.statusDetail.contains("Reinstall Link Control"))
        XCTAssertTrue(controller.logs.contains(executable.path))
        XCTAssertEqual(allocations, 0)
        XCTAssertFalse(controller.isDaemonRunning)
        controller.applicationDidLaunch()
        XCTAssertEqual(allocations, 0, "Launch is attempted once even if the bundle is broken")
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: executable.path)
        controller.retryConnection()
        try await waitUntil { controller.state == .connected }
        XCTAssertEqual(controller.statusLabel, "Connect camera")
        XCTAssertFalse(controller.controlsEnabled)
        XCTAssertEqual(allocations, 1)
        await controller.shutdown()
    }

    func testMissingHelperDoesNotFallBackToExternalDaemon() async throws {
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let bundle = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        let controller = AppController(connection: DaemonConnection(url: url), bundleURL: bundle)
        controller.applicationDidLaunch()
        try await waitUntil { controller.canRetryConnection }
        XCTAssertTrue(controller.statusDetail.contains("Reinstall Link Control"))
        XCTAssertTrue(server.snapshot().isEmpty)
        XCTAssertFalse(controller.isDaemonRunning)
        await controller.shutdown()
    }

    func testEarlyHelperExitRetriesNewPortWithoutConnectingToCompetingListener() async throws {
        let competingServer = try MockDaemon()
        let competingURL = try await competingServer.start()
        defer { competingServer.stop() }
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let bundle = try makeBundle(script: """
            #!/bin/sh
            if [ "$2" = "\(competingURL.port!)" ]; then exit 1; fi
            trap 'exit 0' TERM
            printf '[main] linkctl-daemon running on port %s\\n' "$2"
            while :; do /bin/sleep 0.1; done
            """)
        defer { try? FileManager.default.removeItem(at: bundle) }
        var allocations = 0
        let controller = AppController(bundleURL: bundle, portProvider: {
            allocations += 1
            return UInt16(allocations == 1 ? competingURL.port! : url.port!)
        })
        controller.applicationDidLaunch()
        try await waitUntil { controller.controlsEnabled }
        XCTAssertEqual(allocations, 2)
        XCTAssertTrue(competingServer.snapshot().isEmpty)
        XCTAssertTrue(controller.isDaemonRunning)
        await controller.shutdown()
    }

    func testRepeatedHelperFailureIsBoundedAndLeavesNoOwnedProcess() async throws {
        let bundle = try makeBundle(script: "#!/bin/sh\nexit 1\n")
        defer { try? FileManager.default.removeItem(at: bundle) }
        var allocations = 0
        let controller = AppController(bundleURL: bundle, portProvider: {
            allocations += 1
            return try PrivateDaemonEndpoint.availablePort()
        })
        controller.applicationDidLaunch()
        try await waitUntil { controller.canRetryConnection }
        XCTAssertEqual(allocations, 3)
        XCTAssertFalse(controller.isDaemonRunning)
        XCTAssertFalse(controller.controlsEnabled)
        XCTAssertTrue(controller.statusDetail.contains("Retry connection"))
        await controller.shutdown()
    }

    func testAliveHelperWithoutReadinessNeverConnectsToCompetingListener() async throws {
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let bundle = try makeBundle(script: """
            #!/bin/sh
            trap 'exit 0' TERM
            while :; do /bin/sleep 0.1; done
            """)
        defer { try? FileManager.default.removeItem(at: bundle) }
        let controller = AppController(bundleURL: bundle, portProvider: { UInt16(url.port!) }, startupTimeout: .milliseconds(200))
        var connectedStates = 0
        let observation = controller.$state.sink { if $0 == .connected { connectedStates += 1 } }
        defer { observation.cancel() }
        controller.applicationDidLaunch()
        try await waitUntil { controller.isDaemonRunning }
        XCTAssertEqual(controller.state, .connecting)
        XCTAssertTrue(server.snapshot().isEmpty)
        try await waitUntil { controller.canRetryConnection }
        XCTAssertTrue(server.snapshot().isEmpty, "Never contact a listener until the owned child reports binding successfully")
        XCTAssertTrue(controller.logs.contains("post-bind readiness"))
        XCTAssertEqual(connectedStates, 0)
        XCTAssertFalse(controller.controlsEnabled)
        XCTAssertFalse(controller.isDaemonRunning)
        await controller.shutdown()
    }

    func testRetryReplacesOwnedHelperAndOldCallbacksCannotDisableNewConnection() async throws {
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let bundle = try makeBundle()
        defer { try? FileManager.default.removeItem(at: bundle) }
        var allocations = 0
        let controller = AppController(bundleURL: bundle, portProvider: { allocations += 1; return UInt16(url.port!) })
        controller.applicationDidLaunch()
        try await waitUntil { controller.canJog }
        controller.center()
        try await waitUntil { server.snapshot().contains("center") }
        controller.retryConnection()
        controller.retryConnection()
        try await waitUntil { controller.controlsEnabled && allocations == 2 }
        try await Task.sleep(for: .milliseconds(600))
        XCTAssertTrue(controller.isDaemonRunning)
        XCTAssertEqual(controller.state, .connected)
        XCTAssertEqual(server.snapshot().filter { $0 == "stop" }.count, 1)
        XCTAssertTrue(server.snapshot().contains("reply:stop"))
        await controller.shutdown()
        XCTAssertFalse(controller.isDaemonRunning)
    }

    func testShutdownDuringAutomaticStartupDoesNotLeaveAHelperOrReconnect() async throws {
        let bundle = try makeBundle(script: """
            #!/bin/sh
            trap 'exit 0' TERM
            while :; do /bin/sleep 0.1; done
            """)
        defer { try? FileManager.default.removeItem(at: bundle) }
        let controller = AppController(bundleURL: bundle)
        controller.applicationDidLaunch()
        try await waitUntil { controller.isDaemonRunning }
        await controller.shutdown()
        try await Task.sleep(for: .milliseconds(500))
        XCTAssertFalse(controller.isDaemonRunning)
        XCTAssertEqual(controller.state, .offline)
        XCTAssertFalse(controller.canRetryConnection)
        controller.applicationDidLaunch()
        controller.retryConnection()
        XCTAssertFalse(controller.isDaemonRunning)
    }

    func testShutdownReapsOwnedHelperThatIgnoresGracefulTermination() async throws {
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let bundle = try makeBundle(script: """
            #!/bin/sh
            trap '' TERM
            printf '[main] linkctl-daemon running on port %s\\n' "$2"
            while :; do /bin/sleep 0.1; done
            """)
        defer { try? FileManager.default.removeItem(at: bundle) }
        let controller = AppController(bundleURL: bundle, portProvider: { UInt16(url.port!) })
        controller.applicationDidLaunch()
        try await waitUntil { controller.controlsEnabled }
        let start = Date()
        await controller.shutdown()
        XCTAssertLessThan(Date().timeIntervalSince(start), 4)
        XCTAssertTrue(controller.logs.contains("sending SIGKILL"))
        XCTAssertFalse(controller.isDaemonRunning)
        XCTAssertTrue(server.snapshot().contains("reply:stop"))
    }

    func testPortAllocatorChoosesEphemeralPort() throws {
        let port = try PrivateDaemonEndpoint.availablePort()
        XCTAssertGreaterThan(port, 1024)
        XCTAssertNotEqual(port, 9000)
    }

    func testSplitReadinessLineWaitsForNewlineBeforeConnecting() async throws {
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let bundle = try makeBundle(script: """
            #!/bin/sh
            trap 'exit 0' TERM
            printf '[main] linkctl-daemon running on '
            /bin/sleep 0.1
            printf 'port %s' "$2"
            printf 'waiting-for-newline\\n' >&2
            while [ ! -f "$0.continue" ]; do /bin/sleep 0.02; done
            printf '\\n'
            while :; do /bin/sleep 0.1; done
            """)
        defer { try? FileManager.default.removeItem(at: bundle) }
        let controller = AppController(bundleURL: bundle, portProvider: { UInt16(url.port!) })
        controller.applicationDidLaunch()
        try await waitUntil { controller.logs.contains("waiting-for-newline") }
        XCTAssertTrue(server.snapshot().isEmpty)
        XCTAssertFalse(controller.controlsEnabled)
        try Data().write(to: bundle.appendingPathComponent("Contents/Helpers/linkctl-daemon.continue"))
        try await waitUntil { controller.controlsEnabled }
        await controller.shutdown()
    }

    func testReadinessOnStderrDoesNotAuthorizeConnection() async throws {
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let bundle = try makeBundle(script: """
            #!/bin/sh
            trap 'exit 0' TERM
            printf '[main] linkctl-daemon running on port %s\\n' "$2" >&2
            while :; do /bin/sleep 0.1; done
            """)
        defer { try? FileManager.default.removeItem(at: bundle) }
        let controller = AppController(bundleURL: bundle, portProvider: { UInt16(url.port!) }, startupTimeout: .milliseconds(150))
        controller.applicationDidLaunch()
        try await waitUntil { controller.canRetryConnection }
        XCTAssertTrue(controller.logs.contains("[main] linkctl-daemon running on port \(url.port!)"))
        XCTAssertTrue(server.snapshot().isEmpty)
        XCTAssertFalse(controller.isDaemonRunning)
        await controller.shutdown()
    }

    func testReadinessFromPreviousLaunchCannotAuthorizeRetry() async throws {
        let server = try MockDaemon()
        let url = try await server.start()
        defer { server.stop() }
        let bundle = try makeBundle(script: """
            #!/bin/sh
            trap 'exit 0' TERM
            if [ ! -f "$0.started" ]; then
                /usr/bin/touch "$0.started"
                printf '[main] linkctl-daemon running on port %s\\n' "$2"
            fi
            while :; do /bin/sleep 0.1; done
            """)
        defer { try? FileManager.default.removeItem(at: bundle) }
        let controller = AppController(bundleURL: bundle, portProvider: { UInt16(url.port!) }, startupTimeout: .milliseconds(200))
        controller.applicationDidLaunch()
        try await waitUntil { controller.controlsEnabled }
        let previousStatusCount = server.snapshot().filter { $0 == "status" }.count
        controller.retryConnection()
        try await waitUntil { controller.canRetryConnection }
        XCTAssertEqual(server.snapshot().filter { $0 == "status" }.count, previousStatusCount)
        XCTAssertTrue(controller.logs.contains("[main] linkctl-daemon running on port \(url.port!)"))
        XCTAssertFalse(controller.controlsEnabled)
        XCTAssertFalse(controller.isDaemonRunning)
        await controller.shutdown()
    }

    private enum TestError: Error { case timeout }
}

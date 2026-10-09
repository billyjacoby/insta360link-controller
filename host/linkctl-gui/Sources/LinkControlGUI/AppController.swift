import AppKit
import Darwin
import Foundation
import LinkControlCore

@MainActor final class AppController: ObservableObject {
    enum State: Equatable {
        case offline, connecting, connected, disconnecting, error(String)
    }

    @Published private(set) var state: State = .offline
    @Published private(set) var cameraConnected = false
    @Published private(set) var reportedZoom = 100
    @Published private(set) var isDaemonRunning = false
    @Published private(set) var isStoppingDaemon = false
    @Published private(set) var motionActive = false
    @Published private(set) var logs = ""
    @Published var speed = 8.0
    @Published private(set) var settings: [CameraSetting] = []
    @Published private(set) var isLoadingSettings = false
    @Published private(set) var savingSettingID: String?
    @Published private(set) var settingsError: String?
    @Published private(set) var settingsEpoch = UUID()
    @Published private(set) var isSwitchingMode = false
    @Published private(set) var automationMessage = "Automation ready."
    @Published private(set) var automationFailed = false
    @Published private(set) var hasLoadedSettings = false
    private var automationPending: [AutomationRequest] = []
    private var automationTask: Task<Void, Never>?
    private var automationStopTask: Task<Void, Never>?
    private var automationCurrent: AutomationRequest?
    private var automationEpoch = UUID()
    private var manualAutomationEpoch = UUID()
    private var automationModeOwner: UUID?
    private var automationSettingsOwner: UUID?
    private var settingsRequestOperation = UUID()
    private let automationTimeout: Duration

    private let connection: DaemonConnection
    private let bundleURL: URL
    private let portProvider: () throws -> UInt16
    private let startupTimeout: Duration
    private let modePollInterval: Duration
    private let modeTransitionTimeout: Duration
    private let commands = CommandQueue()
    private let logBuffer = BoundedLogBuffer()
    private var process: Process?
    private var terminatingProcess: Process?
    private var outputPipe: Pipe?
    private var errorPipe: Pipe?
    private var logRefresh: Task<Void, Never>?
    private var connectTask: Task<Void, Never>?
    private var startupTask: Task<Void, Never>?
    private var disconnectTask: Task<Void, Never>?
    private var statusPoll: Task<Void, Never>?
    private var motionDeadline: Task<Void, Never>?
    private var modeTask: Task<Void, Never>?
    private var modeOperation = UUID()
    private var generation = UUID()
    private var lastStatus = Date.distantPast
    private var shuttingDown = false
    private var didAutoStart = false

    init(
        connection: DaemonConnection = DaemonConnection(),
        bundleURL: URL = Bundle.main.bundleURL,
        portProvider: @escaping () throws -> UInt16 = PrivateDaemonEndpoint.availablePort,
        startupTimeout: Duration = .seconds(20),
        modePollInterval: Duration = .milliseconds(750),
        modeTransitionTimeout: Duration = .seconds(15),
        automationTimeout: Duration = .seconds(25)
    ) {
        self.connection = connection
        self.bundleURL = bundleURL
        self.portProvider = portProvider
        self.startupTimeout = startupTimeout
        self.modePollInterval = modePollInterval
        self.modeTransitionTimeout = modeTransitionTimeout
        self.automationTimeout = automationTimeout
    }

    var controlsEnabled: Bool { state == .connected && cameraConnected && !shuttingDown }
    var automationCameraReady: Bool { controlsEnabled && hasLoadedSettings }
    var aiMode: CameraSetting? { settings.first { $0.id == "ai_mode" } }
    var modePending: Bool { isSwitchingMode || aiMode?.isPending == true }
    var manualMotionBlocked: Bool { aiMode?.value == 1 || aiMode?.value == 255 }
    var canJog: Bool { controlsEnabled && !motionActive && !settingsBusy && !modePending && !manualMotionBlocked }
    var canZoom: Bool { controlsEnabled && !modePending }
    var settingsBusy: Bool { isLoadingSettings || savingSettingID != nil || isSwitchingMode }
    var canRefreshSettings: Bool { controlsEnabled && !settingsBusy && !motionActive }
    var canEditSettings: Bool { canRefreshSettings && !modePending }
    var canConnect: Bool {
        !shuttingDown && !isStoppingDaemon && (state == .offline || isError)
    }
    private var isError: Bool { if case .error = state { return true }; return false }

    var canRetryConnection: Bool {
        !shuttingDown && !isStoppingDaemon && startupTask == nil && state != .connecting && state != .disconnecting
    }

    var statusLabel: String {
        switch state {
        case .connected: cameraConnected ? "Camera ready" : "Connect camera"
        case .connecting: "Connecting camera…"
        case .disconnecting: "Restarting connection…"
        case .offline: "Connect camera"
        case .error: "Connection needs attention"
        }
    }

    var statusDetail: String {
        switch state {
        case .connected where cameraConnected: "Your supported Insta360 camera is ready for controls."
        case .connected, .offline: "Connect an Insta360 Link or Link 2 Pro by USB."
        case .connecting, .disconnecting: "Looking for your Insta360 camera…"
        case let .error(message): message
        }
    }

    /// Called by the application delegate, never by view appearance callbacks.
    func applicationDidLaunch() {
        guard !didAutoStart else { return }
        didAutoStart = true
        retryConnection()
    }

    func retryConnection() {
        guard canRetryConnection else { return }
        // Capture the connected state before displaying startup so a retry still
        // sends and acknowledges the final stop on the previous connection.
        let disconnected = disconnect()
        state = .connecting
        cameraConnected = false
        startupTask = Task { [weak self] in
            guard let self else { return }
            defer { self.startupTask = nil }
            await disconnected.value
            await self.terminateOwnedDaemon()
            guard !Task.isCancelled, !self.shuttingDown else { return }
            guard self.process == nil else {
                self.failConnection("The camera connection is still closing. Please try again.")
                return
            }
            let executable = self.bundleURL.appendingPathComponent("Contents/Helpers/linkctl-daemon")
            guard FileManager.default.isExecutableFile(atPath: executable.path) else {
                self.log("Bundled helper missing or not executable: \(executable.path)")
                self.failConnection("The camera helper is missing or cannot run. Reinstall Link Control and try again.")
                return
            }
            // USB probing precedes binding. Wait for this launch's post-bind
            // stdout marker before contacting the port, even if a peer answers.
            for attempt in 1...3 {
                guard !Task.isCancelled, !self.shuttingDown else { return }
                self.generation = UUID()
                let token = self.generation
                self.state = .connecting
                do {
                    let port = try self.portProvider()
                    let endpoint = URL(string: "ws://127.0.0.1:\(port)")!
                    let (child, readiness) = try self.startOwnedDaemon(at: executable, port: port)
                    try await self.waitForReadiness(readiness, child: child, token: token)
                    try await self.establishConnection(attempts: 8, token: token, endpoint: endpoint, child: child)
                    return
                } catch {
                    guard !Task.isCancelled, !self.shuttingDown else { return }
                    self.log("Helper startup attempt \(attempt)/3 failed: \(error.localizedDescription)")
                    await self.disconnect().value
                    await self.terminateOwnedDaemon()
                    guard !Task.isCancelled, !self.shuttingDown else { return }
                    if self.process != nil { break }
                }
            }
            self.failConnection("Could not connect to the camera. Check its USB connection, then choose Retry connection. If this continues, reinstall Link Control.")
        }
    }

    private func waitForReadiness(_ readiness: DaemonReadiness, child: Process, token: UUID) async throws {
        let clock = ContinuousClock()
        let deadline = clock.now.advanced(by: startupTimeout)
        while true {
            guard generation == token, !Task.isCancelled, !shuttingDown else { throw CancellationError() }
            guard process === child, child.isRunning else { throw StartupError.helperExited }
            if readiness.isReady { return }
            guard clock.now < deadline else { throw StartupError.readinessTimedOut }
            try await Task.sleep(for: .milliseconds(25))
        }
    }

    private func startOwnedDaemon(at url: URL, port: UInt16) throws -> (Process, DaemonReadiness) {
        let child = Process()
        child.executableURL = url
        child.arguments = ["--port", String(port), "--no-control", "--loopback", "--auto-camera", "--no-mdns"]
        let pipe = Pipe()
        let errors = Pipe()
        let readiness = DaemonReadiness(port: port)
        child.standardOutput = pipe
        child.standardError = errors
        let buffer = logBuffer
        pipe.fileHandleForReading.readabilityHandler = { handle in
            let data = handle.availableData
            if data.isEmpty { handle.readabilityHandler = nil; return }
            readiness.append(data)
            buffer.append(String(decoding: data, as: UTF8.self))
        }
        errors.fileHandleForReading.readabilityHandler = { handle in
            let data = handle.availableData
            if data.isEmpty { handle.readabilityHandler = nil; return }
            buffer.append(String(decoding: data, as: UTF8.self))
        }
        child.terminationHandler = { [weak self] child in
            Task { @MainActor in
                guard let self, self.process === child else { return }
                let expectedExit = self.terminatingProcess === child
                self.log("Owned daemon exited (status \(child.terminationStatus)).")
                self.releaseOwnedDaemon(child)
                if !expectedExit && self.startupTask == nil && !self.isStoppingDaemon && !self.shuttingDown && self.state != .disconnecting {
                    self.failConnection("The camera connection stopped. Choose Retry connection to reconnect.")
                }
            }
        }
        do {
            try child.run()
            process = child
            outputPipe = pipe
            errorPipe = errors
            isDaemonRunning = true
            log("Started \(url.lastPathComponent) \((child.arguments ?? []).joined(separator: " ")).")
            startLogRefresh()
            return (child, readiness)
        } catch {
            pipe.fileHandleForReading.readabilityHandler = nil
            errors.fileHandleForReading.readabilityHandler = nil
            throw error
        }
    }

    func connect(attempts: Int = 3) {
        guard canConnect else { return }
        generation = UUID()
        let token = generation
        state = .connecting
        cameraConnected = false
        startLogRefresh()
        connectTask = Task { [weak self] in
            guard let self else { return }
            do {
                try await self.establishConnection(attempts: attempts, token: token)
            } catch {
                guard self.generation == token, !Task.isCancelled else { return }
                self.log(error.localizedDescription)
                self.failConnection("Could not connect to the camera. Choose Retry connection to try again.")
            }
        }
    }

    private func establishConnection(attempts: Int, token: UUID, endpoint: URL? = nil, child: Process? = nil) async throws {
        await commands.drain()
        for attempt in 1...attempts {
            guard generation == token, !Task.isCancelled else { throw CancellationError() }
            if let child, process !== child || !child.isRunning { throw StartupError.helperExited }
            log("Connecting to \(endpoint?.absoluteString ?? "test endpoint") (attempt \(attempt)/\(attempts))…")
            do {
                let reply = try await connection.connect(to: endpoint)
                if let child, process !== child || !child.isRunning { throw StartupError.helperExited }
                guard generation == token, !Task.isCancelled else { throw CancellationError() }
                state = .connected
                handle(.success(reply), token: token)
                log("WebSocket ready; received camera status.")
                await connection.receiveReplies { [weak self] result in
                    await self?.handle(result, token: token)
                }
                guard generation == token else { throw CancellationError() }
                beginPolling(token: token)
                return
            } catch {
                guard generation == token, !Task.isCancelled else { throw CancellationError() }
                if let child, process !== child || !child.isRunning { throw StartupError.helperExited }
                if attempt == attempts { throw error }
                try await Task.sleep(for: .milliseconds(400))
            }
        }
    }

    private enum StartupError: LocalizedError {
        case helperExited, readinessTimedOut
        var errorDescription: String? {
            switch self {
            case .helperExited: "The bundled camera helper exited before its connection was ready."
            case .readinessTimedOut: "Timed out waiting for the camera helper's post-bind readiness message."
            }
        }
    }

    /// Invalidates queued motion immediately, then appends a final stop and waits
    /// for acknowledgement before closing the transport.
    @discardableResult
    func disconnect() -> Task<Void, Never> {
        invalidateAutomation("Connection closed; pending automation cancelled.")
        if let disconnectTask { return disconnectTask }
        let needsStop = state == .connected
        generation = UUID()
        let token = generation
        cancelBackgroundCommands()
        state = .disconnecting
        cameraConnected = false
        clearSettings(invalidatePending: false)
        let task = commands.submit { [weak self] in
            guard let self else { return }
            if needsStop {
                do {
                    try await self.connection.send(.stop)
                    self.log("Final stop acknowledged before disconnect.")
                } catch {
                    self.log("Final stop could not be sent: \(error.localizedDescription)")
                }
            }
            await self.connection.disconnect()
            guard self.generation == token else { return }
            self.state = .offline
            self.disconnectTask = nil
            self.log("Disconnected.")
        }
        disconnectTask = task
        return task
    }

    func stopOwnedDaemon() async {
        guard !isStoppingDaemon else { return }
        isStoppingDaemon = true
        await disconnect().value
        await terminateOwnedDaemon()
        isStoppingDaemon = false
    }

    func shutdown() async {
        shuttingDown = true
        startupTask?.cancel()
        await disconnect().value
        await terminateOwnedDaemon()
        logRefresh?.cancel()
        logRefresh = nil
    }

    private func terminateOwnedDaemon() async {
        guard let child = process else { return }
        guard child.isRunning else { releaseOwnedDaemon(child); return }
        log("Terminating the daemon launched by this app.")
        terminatingProcess = child
        child.terminate()
        // Give the daemon time to run its own stop/USB cleanup without blocking UI.
        for _ in 0..<40 {
            if !child.isRunning { releaseOwnedDaemon(child); return }
            try? await Task.sleep(for: .milliseconds(50))
        }
        log("Owned helper did not exit after SIGTERM; sending SIGKILL.")
        if process === child, child.isRunning { kill(child.processIdentifier, SIGKILL) }
        for _ in 0..<20 {
            if !child.isRunning { releaseOwnedDaemon(child); return }
            try? await Task.sleep(for: .milliseconds(50))
        }
        log("Helper has not exited; process ownership retained.")
    }

    private func releaseOwnedDaemon(_ child: Process) {
        guard process === child else { return }
        outputPipe?.fileHandleForReading.readabilityHandler = nil
        outputPipe = nil
        errorPipe?.fileHandleForReading.readabilityHandler = nil
        errorPipe = nil
        process = nil
        terminatingProcess = nil
        isDaemonRunning = false
    }

    private func beginPolling(token: UUID) {
        statusPoll?.cancel()
        statusPoll = Task { [weak self] in
            while !Task.isCancelled {
                do { try await Task.sleep(for: .seconds(2)) } catch { return }
                guard let self, self.generation == token, self.state == .connected else { return }
                guard Date().timeIntervalSince(self.lastStatus) < 8 else {
                    self.failConnection("Daemon stopped answering status requests.")
                    return
                }
                self.enqueue(.status)
            }
        }
    }

    private func handle(_ result: Result<DaemonReply, Error>, token: UUID) {
        guard generation == token, state == .connected else { return }
        switch result {
        case let .success(.status(connected, zoom)):
            lastStatus = Date()
            let wasConnected = cameraConnected
            if cameraConnected && !connected { stopMotion() }
            cameraConnected = connected
            reportedZoom = min(400, max(100, zoom))
            if wasConnected && !connected { clearSettings() }
            if !wasConnected && connected { refreshSettings() }
        case let .success(.error(message)):
            // Disable immediately. A rejected in-flight command also fails its
            // awaiting sender; an unsolicited error can recover on fresh status.
            let shouldStop = cameraConnected
            cameraConnected = false
            clearSettings()
            motionDeadline?.cancel()
            motionDeadline = nil
            motionActive = false
            if shouldStop { enqueue(.stop) }
            log("Daemon error: \(message)")
        // Settings are applied only by their awaiting request, which captures
        // both connection and camera epochs. Unsolicited/stale replies are inert.
        case .success(.acknowledgement), .success(.settings), .success(.settingsError): break
        case let .failure(error): failConnection("Connection lost: \(error.localizedDescription)")
        }
    }

    private func failConnection(_ message: String) {
        invalidateAutomation(message)
        generation = UUID()
        cancelBackgroundCommands()
        cameraConnected = false
        clearSettings(invalidatePending: false)
        state = .error(message)
        log(message)
        // This is queued before any reconnect can submit new work.
        commands.submit { [connection] in await connection.disconnect() }
    }

    private func cancelBackgroundCommands() {
        connectTask?.cancel()
        connectTask = nil
        statusPoll?.cancel()
        statusPoll = nil
        motionDeadline?.cancel()
        motionDeadline = nil
        motionActive = false
    }

    @discardableResult
    private func enqueue(_ command: DaemonCommand) -> Task<Void, Never> {
        let token = generation
        return commands.submit { [weak self] in
            guard let self, self.generation == token, self.state == .connected else { return }
            switch command {
            case .panTilt, .center:
                guard self.cameraConnected, !self.modePending, !self.manualMotionBlocked else { return }
            case .zoom:
                guard self.canZoom else { return }
            case .settingsGet, .settingSet:
                guard self.cameraConnected else { return }
            case .stop, .status: break
            }
            do {
                try await self.connection.send(command)
                if command != .status { self.log("Sent \((try? command.jsonString()) ?? "command")") }
            } catch {
                guard self.generation == token else { return }
                self.failConnection(error.localizedDescription)
            }
        }
    }

    func move(pan: Direction = .stopped, tilt: Direction = .stopped) {
        guard canJog else { return }
        motionActive = true
        motionDeadline?.cancel()
        let amount = Int(speed)
        let sent = enqueue(.panTilt(
            panDirection: pan, panSpeed: pan == .stopped ? 0 : amount,
            tiltDirection: tilt, tiltSpeed: tilt == .stopped ? 0 : min(amount, 20)
        ))
        scheduleStop(after: .milliseconds(350), sent: sent)
    }

    func center() {
        guard canJog else { return }
        motionActive = true
        motionDeadline?.cancel()
        scheduleStop(after: .milliseconds(500), sent: enqueue(.center))
    }

    private func scheduleStop(after duration: Duration, sent: Task<Void, Never>) {
        let token = generation
        motionDeadline = Task { [weak self] in
            await sent.value
            do { try await Task.sleep(for: duration) } catch { return }
            guard let self, self.generation == token, !Task.isCancelled else { return }
            self.stopMotion()
        }
    }

    func stopMotion() {
        let cancelledManual = automationCurrent?.action.isManualMove == true || automationPending.contains { $0.action.isManualMove }
        manualAutomationEpoch = UUID()
        automationPending.removeAll { $0.action.isManualMove }
        if automationCurrent?.action.isManualMove == true { automationTask?.cancel() }
        if cancelledManual { reportAutomation("Pending manual automation cancelled.") }
        motionDeadline?.cancel()
        motionDeadline = nil
        motionActive = false
        guard state == .connected else { return }
        enqueue(.stop)
    }

    func setZoom(_ value: Int) {
        guard canZoom else { return }
        enqueue(.zoom(value))
    }

    func refreshSettings() {
        guard canRefreshSettings else { return }
        isLoadingSettings = true
        requestSettings(.settingsGet)
    }

    func setSetting(id: String, value: Int) {
        guard canEditSettings,
              let control = settings.first(where: { $0.id == id }),
               control.canEdit, control.accepts(value) else { return }
        if id == "ai_mode" {
            guard value != control.value else { return }
            beginModeOperation(target: value)
            return
        }
        savingSettingID = id
        requestSettings(.settingSet(id: id, value: value))
    }

    func resetSetting(id: String) {
        guard let control = settings.first(where: { $0.id == id }),
              control.canReset, let value = control.defaultValue else { return }
        setSetting(id: id, value: value)
    }

    private func clearSettings(invalidatePending: Bool = true) {
        if invalidatePending { invalidateAutomation("Camera changed or disconnected; pending automation cancelled.") }
        hasLoadedSettings = false
        settingsRequestOperation = UUID()
        modeOperation = UUID()
        modeTask?.cancel()
        modeTask = nil
        isSwitchingMode = false
        settingsEpoch = UUID()
        settings = []
        isLoadingSettings = false
        savingSettingID = nil
        settingsError = nil
    }

    @discardableResult
    private func requestSettings(_ command: DaemonCommand, automation: AutomationRequest? = nil) -> Task<Void, Never> {
        settingsError = nil
        let operation = UUID()
        settingsRequestOperation = operation
        let token = generation
        let epoch = settingsEpoch
        return commands.submit { [weak self] in
            guard let self, self.settingsRequestOperation == operation, self.generation == token, self.settingsEpoch == epoch,
                  self.controlsEnabled else { return }
            do {
                if let automation { try self.checkAutomation(automation) }
                let reply = try await self.connection.send(command)
                guard self.settingsRequestOperation == operation, self.generation == token, self.settingsEpoch == epoch,
                      self.controlsEnabled else { return }
                if let automation { try self.checkAutomation(automation) }
                if case let .settings(controls) = reply {
                    self.settings = controls
                    self.hasLoadedSettings = true
                }
                self.isLoadingSettings = false
                self.savingSettingID = nil
                if self.aiMode?.isPending == true { self.beginModeOperation(target: nil) }
            } catch {
                guard self.settingsRequestOperation == operation, self.generation == token, self.settingsEpoch == epoch,
                      self.controlsEnabled else { return }
                self.isLoadingSettings = false
                self.savingSettingID = nil
                if case DaemonConnection.ConnectionError.settings(let message) = error {
                    self.settingsError = message
                    self.log("Camera settings: \(message)")
                } else {
                    self.failConnection(error.localizedDescription)
                }
            }
        }
    }

    /// Mode settling lives outside the command FIFO. Only individual USB requests
    /// are queued; Stop and status requests can run during every polling sleep.
    private func beginModeOperation(target: Int?) {
        guard modeTask == nil, controlsEnabled else { return }
        let operation = UUID()
        modeOperation = operation
        let token = generation
        let epoch = settingsEpoch
        isSwitchingMode = true
        savingSettingID = target == nil ? nil : "ai_mode"
        settingsError = nil
        modeTask = Task { [weak self] in
            guard let self else { return }
            defer {
                if self.modeOperation == operation {
                    self.modeTask = nil
                    self.isSwitchingMode = false
                    self.savingSettingID = nil
                }
            }
            do {
                try self.checkModeOperation(operation, token: token, epoch: epoch)
                if let target {
                    // The Pro firmware ignores direct switches between AI modes.
                    if self.aiMode?.value != 0 && target != 0 {
                        try await self.modeRequest(.settingSet(id: "ai_mode", value: 0), operation: operation, token: token, epoch: epoch)
                        try self.checkModeOperation(operation, token: token, epoch: epoch)
                        try await self.waitForMode(0, operation: operation, token: token, epoch: epoch)
                        try self.checkModeOperation(operation, token: token, epoch: epoch)
                    }
                    try await self.modeRequest(.settingSet(id: "ai_mode", value: target), operation: operation, token: token, epoch: epoch)
                    try self.checkModeOperation(operation, token: token, epoch: epoch)
                }
                try await self.waitForMode(target, operation: operation, token: token, epoch: epoch)
                try self.checkModeOperation(operation, token: token, epoch: epoch)
            } catch is CancellationError {
                // A cancelled operation must never retry a write on a new camera.
            } catch {
                guard self.modeOperation == operation, self.generation == token,
                      self.settingsEpoch == epoch, self.controlsEnabled else { return }
                if case DaemonConnection.ConnectionError.settings(let message) = error {
                    self.settingsError = message
                    self.log("Camera mode: \(message)")
                } else {
                    self.failConnection(error.localizedDescription)
                }
            }
        }
    }

    private func checkModeOperation(_ operation: UUID, token: UUID, epoch: UUID) throws {
        guard !Task.isCancelled, modeOperation == operation, generation == token,
              settingsEpoch == epoch, controlsEnabled else { throw CancellationError() }
    }

    private func modeRequest(_ command: DaemonCommand, operation: UUID, token: UUID, epoch: UUID) async throws {
        try checkModeOperation(operation, token: token, epoch: epoch)
        var result: Result<DaemonReply, Error> = .failure(CancellationError())
        let request = commands.submit { [weak self] in
            guard let self else { return }
            do {
                try self.checkModeOperation(operation, token: token, epoch: epoch)
                let reply = try await self.connection.send(command)
                try self.checkModeOperation(operation, token: token, epoch: epoch)
                result = .success(reply)
            } catch { result = .failure(error) }
        }
        await request.value
        try checkModeOperation(operation, token: token, epoch: epoch)
        if case let .settings(controls) = try result.get() { settings = controls }
    }

    private func waitForMode(_ target: Int?, operation: UUID, token: UUID, epoch: UUID) async throws {
        let clock = ContinuousClock()
        let deadline = clock.now.advanced(by: modeTransitionTimeout)
        while true {
            try checkModeOperation(operation, token: token, epoch: epoch)
            guard let mode = aiMode, mode.value != nil else {
                throw DaemonConnection.ConnectionError.settings("Camera mode is unavailable. Choose Refresh to read it again.")
            }
            if let error = mode.error, target == nil || mode.value == target || mode.value == 255 {
                throw DaemonConnection.ConnectionError.settings(error)
            }
            if !mode.isPending && (target == nil || mode.value == target) { return }
            guard clock.now < deadline else {
                throw DaemonConnection.ConnectionError.settings("Camera mode did not finish within 15 seconds. Keep preview running, then choose Refresh to check its actual state.")
            }
            try await Task.sleep(for: min(modePollInterval, clock.now.duration(to: deadline)))
            try checkModeOperation(operation, token: token, epoch: epoch)
            try await modeRequest(.settingsGet, operation: operation, token: token, epoch: epoch)
            try checkModeOperation(operation, token: token, epoch: epoch)
        }
    }

    func clearLogs() {
        logBuffer.clear()
        logs = ""
    }

    private func log(_ message: String) {
        logBuffer.append("[\(Date().formatted(date: .omitted, time: .standard))] \(message)\n")
        logs = logBuffer.snapshot()
    }

    private func startLogRefresh() {
        guard logRefresh == nil else { return }
        logRefresh = Task { [weak self] in
            while !Task.isCancelled {
                do { try await Task.sleep(for: .milliseconds(300)) } catch { return }
                guard let self else { return }
                let snapshot = self.logBuffer.snapshot()
                if self.logs != snapshot { self.logs = snapshot }
            }
        }
    }
}

// Startup/mode waits live here, outside the transport FIFO. Only an individual
// acknowledged request occupies CommandQueue, so manual Stop remains responsive.
extension AppController {
    private struct AutomationRequest {
        let action: AutomationAction
        let epoch: UUID
        let manualEpoch: UUID
        let deadline: ContinuousClock.Instant
    }

    struct AutomationFailure: LocalizedError {
        let message: String
        var errorDescription: String? { message }
    }

    func reportAutomation(_ message: String, failed: Bool = false) {
        automationMessage = message
        automationFailed = failed
        log("Automation: \(message)")
    }

    private func invalidateAutomation(_ reason: String) {
        let hadPending = automationCurrent != nil || !automationPending.isEmpty || automationStopTask != nil
        automationEpoch = UUID()
        manualAutomationEpoch = UUID()
        automationPending.removeAll()
        automationTask?.cancel()
        if hadPending { reportAutomation(reason, failed: true) }
    }

    /// App-only routes are handled by AppDelegate; all camera routes enter here.
    func submitAutomation(_ action: AutomationAction) {
        guard !shuttingDown else {
            reportAutomation("Link Control is shutting down; action cancelled.", failed: true)
            return
        }
        if action == .stop {
            let reconcileSettings = cancelExternalSettingsOperation()
            invalidateAutomation("Pending automation cancelled by Stop.")
            motionDeadline?.cancel()
            motionDeadline = nil
            motionActive = false
            guard state == .connected else {
                reportAutomation("Pending automation cancelled. Stop was not sent: camera service is unavailable.", failed: true)
                return
            }
            guard automationStopTask == nil else { return }
            let request = makeAutomationRequest(action)
            let sent = queuedAutomationSend(.stop, request: request)
            if reconcileSettings {
                // The in-flight write may have applied. Read back after Stop;
                // observing a pending mode never initiates another mode write.
                settings = []
                hasLoadedSettings = false
                isLoadingSettings = true
                requestSettings(.settingsGet)
            }
            automationStopTask = Task { [weak self] in
                guard let self else { return }
                defer { self.automationStopTask = nil }
                do {
                    try await sent.value.get()
                    self.reportAutomation("Acknowledged: Stop manual motion. Pending automation cancelled; already-active AI is not turned off.")
                } catch {
                    if self.automationEpoch == request.epoch {
                        self.reportAutomation("Stop failed: \(error.localizedDescription)", failed: true)
                    }
                }
            }
            return
        }
        guard automationPending.count + (automationCurrent == nil ? 0 : 1) < 32 else {
            reportAutomation("Automation queue is full (32 actions). Try again after it drains.", failed: true)
            return
        }
        automationPending.append(makeAutomationRequest(action))
        reportAutomation("Queued: \(action.label). Waiting for camera readiness if needed.")
        runAutomationQueue()
    }

    private func makeAutomationRequest(_ action: AutomationAction) -> AutomationRequest {
        AutomationRequest(action: action, epoch: automationEpoch, manualEpoch: manualAutomationEpoch,
                          deadline: ContinuousClock.now.advanced(by: automationTimeout))
    }

    /// Only invalidate writes initiated by this router, never an unrelated GUI
    /// mode operation that an external action happened to be waiting behind.
    private func cancelExternalSettingsOperation() -> Bool {
        var cancelled = false
        if let owner = automationModeOwner, modeOperation == owner {
            modeOperation = UUID()
            modeTask?.cancel()
            modeTask = nil
            isSwitchingMode = false
            savingSettingID = nil
            cancelled = true
        }
        if let owner = automationSettingsOwner, settingsRequestOperation == owner {
            settingsRequestOperation = UUID()
            savingSettingID = nil
            isLoadingSettings = false
            cancelled = true
        }
        automationModeOwner = nil
        automationSettingsOwner = nil
        return cancelled
    }

    private func runAutomationQueue() {
        guard automationTask == nil, !automationPending.isEmpty else { return }
        automationTask = Task { [weak self] in
            guard let self else { return }
            while !self.automationPending.isEmpty && !Task.isCancelled {
                let request = self.automationPending.removeFirst()
                self.automationCurrent = request
                do {
                    try await self.performAutomation(request)
                    try self.checkAutomation(request)
                    self.reportAutomation("Acknowledged: \(request.action.label).")
                } catch is CancellationError {
                    // Invalidation/Stop already published the cancellation reason.
                } catch {
                    if self.automationEpoch == request.epoch {
                        self.reportAutomation("\(request.action.label) failed: \(error.localizedDescription)", failed: true)
                    }
                }
                self.automationCurrent = nil
            }
            self.automationTask = nil
            self.runAutomationQueue()
        }
    }

    private func checkAutomation(_ request: AutomationRequest) throws {
        guard !Task.isCancelled, !shuttingDown, automationEpoch == request.epoch || request.action == .stop,
              !request.action.isManualMove || manualAutomationEpoch == request.manualEpoch else { throw CancellationError() }
    }

    private func awaitAutomationReady(_ request: AutomationRequest) async throws {
        while true {
            try checkAutomation(request)
            guard ContinuousClock.now < request.deadline else {
                throw AutomationFailure(message: "Timed out waiting for camera/settings readiness. Open the dashboard and check the connection and camera mode.")
            }
            if case let .error(message) = state { throw AutomationFailure(message: message) }
            if request.action == .refresh, controlsEnabled, !settingsBusy { return }
            if let settingsError, !hasLoadedSettings, !isLoadingSettings { throw AutomationFailure(message: settingsError) }
            if controlsEnabled && hasLoadedSettings && !isLoadingSettings && savingSettingID == nil && !isSwitchingMode {
                return
            }
            try await Task.sleep(for: .milliseconds(20))
        }
    }

    private func performAutomation(_ request: AutomationRequest) async throws {
        try await awaitAutomationReady(request)
        switch request.action {
        case .center, .jog:
            guard canJog else { throw AutomationFailure(message: "Manual motion is unavailable. Wait for motion to stop and select Normal mode if tracking is active.") }
            motionActive = true
            defer {
                if automationEpoch == request.epoch, manualAutomationEpoch == request.manualEpoch { motionActive = false }
            }
            let command: DaemonCommand
            let duration: Duration
            if case let .jog(direction) = request.action {
                let pan: Direction = direction == .left ? .negative : direction == .right ? .positive : .stopped
                let tilt: Direction = direction == .up ? .negative : direction == .down ? .positive : .stopped
                let amount = min(20, max(1, Int(speed)))
                command = .panTilt(panDirection: pan, panSpeed: pan == .stopped ? 0 : amount,
                                   tiltDirection: tilt, tiltSpeed: tilt == .stopped ? 0 : amount)
                duration = .milliseconds(350)
            } else { command = .center; duration = .milliseconds(500) }
            do {
                try await automationSend(command, request: request)
                try await Task.sleep(for: duration)
                try await automationSend(.stop, request: request)
            } catch {
                // A transport timeout may follow a write that physically applied.
                // Invalidation/priority Stop already arrange their own final stop.
                if automationEpoch == request.epoch, manualAutomationEpoch == request.manualEpoch { stopMotion() }
                throw error
            }
        case let .zoom(value):
            guard (100...400).contains(value), canZoom else { throw AutomationFailure(message: "Zoom is unavailable while the camera mode is settling.") }
            try await automationSend(.zoom(value), request: request)
        case let .mode(mode):
            guard canEditSettings, let control = aiMode, control.canEdit, control.accepts(mode.value) else {
                throw AutomationFailure(message: "This camera does not currently support the requested mode. Refresh or wait for its current mode to settle.")
            }
            // A prior setting/write error does not invalidate a valid snapshot.
            // This also makes selecting the already-active mode a fresh no-op.
            settingsError = nil
            if control.value != mode.value {
                setSetting(id: "ai_mode", value: mode.value)
                let owner = modeOperation
                automationModeOwner = owner
                defer { if automationModeOwner == owner { automationModeOwner = nil } }
                await modeTask?.value
                try checkAutomation(request)
            }
            if let settingsError { throw AutomationFailure(message: settingsError) }
            if let error = aiMode?.error { throw AutomationFailure(message: error) }
            guard aiMode?.value == mode.value, !modePending else { throw AutomationFailure(message: "Camera mode did not settle at the requested value.") }
        case let .setting(id, value):
            guard AutomationAction.imageSettingIDs.contains(id), canEditSettings,
                  let control = settings.first(where: { $0.id == id }), control.canEdit, control.accepts(value) else {
                throw AutomationFailure(message: "Setting is unsupported, inactive, read-only, or outside this camera's advertised range/step/options.")
            }
            savingSettingID = id
            let sent = requestSettings(.settingSet(id: id, value: value), automation: request)
            let owner = settingsRequestOperation
            automationSettingsOwner = owner
            defer { if automationSettingsOwner == owner { automationSettingsOwner = nil } }
            await sent.value
            try checkAutomation(request)
            if let settingsError { throw AutomationFailure(message: settingsError) }
            guard settings.first(where: { $0.id == id })?.value == value else {
                throw AutomationFailure(message: "Camera readback did not match the requested setting.")
            }
        case .refresh:
            guard canRefreshSettings else { throw AutomationFailure(message: "Camera settings are busy. Try Refresh again when motion has stopped.") }
            isLoadingSettings = true
            let sent = requestSettings(.settingsGet, automation: request)
            let owner = settingsRequestOperation
            automationSettingsOwner = owner
            defer { if automationSettingsOwner == owner { automationSettingsOwner = nil } }
            await sent.value
            try checkAutomation(request)
            if let settingsError { throw AutomationFailure(message: settingsError) }
        default: throw AutomationFailure(message: "This action must be handled by the application router.")
        }
    }

    private func automationSend(_ command: DaemonCommand, request: AutomationRequest) async throws {
        let result = await queuedAutomationSend(command, request: request).value
        try checkAutomation(request)
        try result.get()
    }

    /// Reserve the FIFO slot synchronously, notably before a later URL can jog.
    private func queuedAutomationSend(_ command: DaemonCommand, request: AutomationRequest) -> Task<Result<Void, Error>, Never> {
        var result: Result<Void, Error> = .failure(CancellationError())
        let token = generation
        let epoch = settingsEpoch
        let task = commands.submit { [weak self] in
            guard let self else { return }
            do {
                try self.checkAutomation(request)
                guard self.generation == token, self.settingsEpoch == epoch, self.state == .connected else { throw CancellationError() }
                if command != .stop {
                    guard self.controlsEnabled, !self.modePending else { throw AutomationFailure(message: "Camera controls became unavailable.") }
                    switch command {
                    case .panTilt, .center:
                        guard !self.manualMotionBlocked else { throw AutomationFailure(message: "Manual motion is blocked by tracking.") }
                    default: break
                    }
                }
                do {
                    _ = try await self.connection.send(command)
                } catch {
                    if self.generation == token {
                        self.failConnection("\(request.action.label) failed: \(error.localizedDescription)")
                    }
                    throw error
                }
                try self.checkAutomation(request)
                guard self.generation == token, self.settingsEpoch == epoch else { throw CancellationError() }
                result = .success(())
            } catch { result = .failure(error) }
        }
        return Task {
            await task.value
            return result
        }
    }
}

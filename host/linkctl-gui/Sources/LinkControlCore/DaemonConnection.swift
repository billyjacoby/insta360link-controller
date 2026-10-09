import Foundation

/// One WebSocket lifetime. A successful connect requires a decoded status reply,
/// not merely calling resume() on a URLSession task.
public actor DaemonConnection {
    private let session: URLSession
    private let url: URL
    private let timeout: Duration
    private var socket: URLSessionWebSocketTask?
    private var generation = UUID()
    private var receiver: Task<Void, Never>?
    private var pending: AsyncThrowingStream<DaemonReply, Error>.Continuation?
    private var expected: ExpectedReply?

    private enum ExpectedReply { case status, acknowledgement, settings }

    public init(url: URL = URL(string: "ws://127.0.0.1:9000")!, timeout: Duration = .seconds(3)) {
        self.url = url
        self.timeout = timeout
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 3
        session = URLSession(configuration: configuration)
    }

    public func connect(to endpoint: URL? = nil) async throws -> DaemonReply {
        disconnect()
        let token = generation
        let task = session.webSocketTask(with: endpoint ?? url, protocols: ["arduino"])
        socket = task
        task.resume()
        let deadline = watchdog(token: token)
        defer { deadline.cancel() }
        do {
            try await task.send(.string(try DaemonCommand.status.jsonString()))
            while true {
                let reply = try await Self.decode(task.receive())
                guard generation == token else { throw CancellationError() }
                switch reply {
                case .status: return reply
                case let .error(message): throw ConnectionError.daemon(message)
                case .acknowledgement, .settings, .settingsError: continue
                }
            }
        } catch {
            if generation == token { disconnect() }
            throw error
        }
    }

    public func receiveReplies(
        onReply: @escaping @Sendable (Result<DaemonReply, Error>) async -> Void
    ) {
        guard let socket else { return }
        receiver?.cancel()
        let token = generation
        receiver = Task { [weak self] in
            while !Task.isCancelled {
                do {
                    let reply = try await Self.decode(socket.receive())
                    guard let self, await self.isCurrent(token), !Task.isCancelled else { return }
                    await self.resolvePending(reply, token: token)
                    await onReply(.success(reply))
                } catch {
                    guard let self, await self.isCurrent(token), !Task.isCancelled else { return }
                    await self.rejectPending(error, token: token)
                    await onReply(.failure(error))
                    return
                }
            }
        }
    }

    /// The caller's FIFO queue owns ordering. The watchdog bounds a stalled send,
    /// including the final stop during application termination.
    @discardableResult
    public func send(_ command: DaemonCommand) async throws -> DaemonReply {
        guard let socket else { throw ConnectionError.notConnected }
        guard pending == nil else { throw ConnectionError.concurrentCommand }
        let token = generation
        let deadline = watchdog(token: token)
        defer {
            deadline.cancel()
            if generation == token { pending = nil; expected = nil }
        }
        switch command {
        case .status: expected = .status
        case .settingsGet, .settingSet: expected = .settings
        default: expected = .acknowledgement
        }
        let replies = AsyncThrowingStream<DaemonReply, Error> { pending = $0 }
        try await socket.send(.string(try command.jsonString()))
        // There are no request IDs. Only the expected response type completes
        // the one outstanding command; status and ACK cannot complete settings.
        for try await reply in replies {
            guard generation == token else { throw CancellationError() }
            return reply
        }
        throw ConnectionError.notConnected
    }

    public func disconnect() {
        generation = UUID()
        pending?.finish(throwing: ConnectionError.notConnected)
        pending = nil
        expected = nil
        receiver?.cancel()
        receiver = nil
        socket?.cancel(with: .goingAway, reason: nil)
        socket = nil
    }

    private func isCurrent(_ token: UUID) -> Bool { generation == token }

    private func resolvePending(_ reply: DaemonReply, token: UUID) {
        guard generation == token else { return }
        switch (expected, reply) {
        case (.acknowledgement, .acknowledgement), (.status, .status), (.settings, .settings):
            pending?.yield(reply)
            pending?.finish()
        case (.settings, let .settingsError(message)):
            pending?.finish(throwing: ConnectionError.settings(message))
        case (_, let .error(message)):
            pending?.finish(throwing: ConnectionError.daemon(message))
        default: break
        }
    }

    private func rejectPending(_ error: Error, token: UUID) {
        guard generation == token else { return }
        pending?.finish(throwing: error)
    }

    private func watchdog(token: UUID) -> Task<Void, Never> {
        Task { [weak self, timeout] in
            do { try await Task.sleep(for: timeout) } catch { return }
            await self?.expire(token: token)
        }
    }

    private func expire(token: UUID) {
        guard generation == token else { return }
        disconnect()
    }

    private static func decode(_ message: URLSessionWebSocketTask.Message) throws -> DaemonReply {
        switch message {
        case let .string(text): return try DaemonReply.decode(Data(text.utf8))
        case let .data(data): return try DaemonReply.decode(data)
        @unknown default: throw ConnectionError.unsupportedFrame
        }
    }

    public enum ConnectionError: LocalizedError {
        case notConnected, unsupportedFrame, concurrentCommand, daemon(String), settings(String)

        public var errorDescription: String? {
            switch self {
            case .notConnected: "Not connected to the local daemon."
            case .unsupportedFrame: "Unsupported WebSocket frame."
            case .concurrentCommand: "Commands must be submitted in order."
            case let .daemon(message), let .settings(message): message
            }
        }
    }
}

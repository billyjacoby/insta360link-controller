import Foundation
import Network

/// A real loopback WebSocket peer. No camera, daemon binary, or external packages.
final class MockDaemon: @unchecked Sendable {
    enum StatusMode { case normal, silent, malformed, warmingUp(Int), settingsOnly }
    private let queue = DispatchQueue(label: "LinkControl.tests.mockDaemon")
    private let listener: NWListener
    private var clients: [NWConnection] = []
    private var events: [String] = []
    private var receivedCommands: [[String: Any]] = []
    private var statusRequests = 0
    private let mode: StatusMode
    private let ackDelay: Double
    private var cameraConnected: Bool
    private let acknowledgeCommands: Bool
    private var settingsReply: String
    private let settingsDelay: Double
    private let settingsPreamble: [String]
    private var scriptedSettingsReplies: [String] = []

    static let defaultSettings = #"{"type":"settings","controls":[{"id":"brightness","label":"Brightness","group":"image","kind":"integer","value":0,"min":-64,"max":64,"step":1,"default":0,"readable":true,"writable":true,"active":true,"unit":"","options":[],"error":null},{"id":"focus_auto","label":"Autofocus","group":"focus","kind":"boolean","value":1,"min":0,"max":1,"step":1,"default":1,"readable":true,"writable":true,"active":true,"unit":"","options":[],"error":null},{"id":"focus_absolute","label":"Focus","group":"focus","kind":"integer","value":100,"min":0,"max":255,"step":1,"default":100,"readable":true,"writable":true,"active":false,"unit":"","options":[],"error":null}]}"#

    init(mode: StatusMode = .normal, ackDelay: Double = 0, cameraConnected: Bool = true,
         acknowledgeCommands: Bool = true, settingsReply: String = MockDaemon.defaultSettings,
         settingsDelay: Double = 0, settingsPreamble: [String] = []) throws {
        self.mode = mode
        self.ackDelay = ackDelay
        self.cameraConnected = cameraConnected
        self.acknowledgeCommands = acknowledgeCommands
        self.settingsReply = settingsReply
        self.settingsDelay = settingsDelay
        self.settingsPreamble = settingsPreamble
        let options = NWProtocolWebSocket.Options()
        options.autoReplyPing = true
        options.setClientRequestHandler(queue) { protocols, _ in
            .init(status: protocols.contains("arduino") ? .accept : .reject, subprotocol: "arduino")
        }
        let parameters = NWParameters(tls: nil, tcp: NWProtocolTCP.Options())
        parameters.defaultProtocolStack.applicationProtocols.insert(options, at: 0)
        parameters.requiredLocalEndpoint = .hostPort(host: "127.0.0.1", port: .any)
        listener = try NWListener(using: parameters)
    }

    func start() async throws -> URL {
        listener.newConnectionHandler = { [weak self] client in
            guard let self else { return }
            self.clients.append(client)
            client.stateUpdateHandler = { [weak self] state in
                if case .ready = state { self?.receive(client) }
            }
            client.start(queue: self.queue)
        }
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            listener.stateUpdateHandler = { [weak self] state in
                switch state {
                case .ready:
                    self?.listener.stateUpdateHandler = nil
                    continuation.resume()
                case let .failed(error):
                    self?.listener.stateUpdateHandler = nil
                    continuation.resume(throwing: error)
                default: break
                }
            }
            listener.start(queue: queue)
        }
        return URL(string: "ws://127.0.0.1:\(listener.port!.rawValue)")!
    }

    func snapshot() -> [String] { queue.sync { events } }
    func commandSnapshot() -> [[String: Any]] { queue.sync { receivedCommands } }

    func setSettingsResponse(_ text: String) { queue.sync { settingsReply = text } }

    /// Responses are consumed by individual settings reads/writes, then the last
    /// snapshot remains available for bounded-poll and stale-readback tests.
    func scriptSettingsResponses(_ replies: [String]) { queue.sync { scriptedSettingsReplies = replies } }

    func setCameraConnected(_ connected: Bool) {
        queue.sync {
            cameraConnected = connected
            for client in clients {
                reply("{\"type\":\"status\",\"camera_connected\":\(connected),\"zoom\":125}", command: "unsolicited-status", to: client)
            }
        }
    }

    func broadcast(_ text: String) {
        queue.sync { for client in clients { reply(text, command: "broadcast", to: client) } }
    }

    func dropClients() {
        queue.sync { clients.forEach { $0.cancel() } }
    }

    func stop() {
        queue.sync {
            clients.forEach { $0.cancel() }
            listener.cancel()
        }
    }

    private func receive(_ client: NWConnection) {
        client.receiveMessage { [weak self] data, context, _, error in
            guard let self else { return }
            let metadata = context?.protocolMetadata(definition: NWProtocolWebSocket.definition) as? NWProtocolWebSocket.Metadata
            if metadata?.opcode == .close {
                self.events.append("close")
                client.cancel()
                return
            }
            guard error == nil, let data else { return }
            self.events.append(metadata?.opcode == .text ? "text" : "non-text")
            if let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
               let command = json["cmd"] as? String {
                self.receivedCommands.append(json)
                self.events.append(command)
                if command == "status" {
                    self.statusRequests += 1
                    switch self.mode {
                    case .silent: break
                    case .malformed: self.reply("{}", command: command, to: client)
                    case .settingsOnly: self.reply(self.settingsReply, command: command, to: client)
                    case let .warmingUp(count) where self.statusRequests <= count:
                        self.reply(#"{"type":"error","message":"warming up"}"#, command: command, to: client)
                    default:
                        self.reply("{\"type\":\"status\",\"camera_connected\":\(self.cameraConnected),\"zoom\":125}", command: command, to: client)
                    }
                } else if command == "settings_get" || command == "setting_set" {
                    if command == "setting_set", let id = json["id"] as? String, let value = json["value"] as? Int {
                        self.events.append("setting_set:\(id)=\(value)")
                        if self.scriptedSettingsReplies.isEmpty { self.updateSetting(id: id, value: value) }
                    }
                    if !self.scriptedSettingsReplies.isEmpty {
                        self.settingsReply = self.scriptedSettingsReplies.removeFirst()
                    }
                    for text in self.settingsPreamble { self.reply(text, command: "settings-preamble", to: client) }
                    let snapshot = self.settingsReply
                    self.queue.asyncAfter(deadline: .now() + self.settingsDelay) { [weak self] in
                        self?.reply(snapshot, command: command, to: client)
                    }
                } else if self.acknowledgeCommands {
                    self.queue.asyncAfter(deadline: .now() + self.ackDelay) { [weak self] in
                        self?.reply(#"{"type":"ack"}"#, command: command, to: client)
                    }
                }
            }
            self.receive(client)
        }
    }

    private func updateSetting(id: String, value: Int) {
        guard var json = try? JSONSerialization.jsonObject(with: Data(settingsReply.utf8)) as? [String: Any],
              var controls = json["controls"] as? [[String: Any]] else { return }
        for index in controls.indices {
            if controls[index]["id"] as? String == id { controls[index]["value"] = value }
            if id == "focus_auto", controls[index]["id"] as? String == "focus_absolute" {
                controls[index]["active"] = value == 0
                controls[index]["writable"] = value == 0
            }
            if id == "white_balance_auto", controls[index]["id"] as? String == "white_balance_temperature" {
                controls[index]["active"] = value == 0
                controls[index]["writable"] = value == 0
            }
        }
        json["controls"] = controls
        if let data = try? JSONSerialization.data(withJSONObject: json) {
            settingsReply = String(decoding: data, as: UTF8.self)
        }
    }

    private func reply(_ text: String, command: String, to client: NWConnection) {
        events.append("reply:\(command)")
        let context = NWConnection.ContentContext(
            identifier: "reply", metadata: [NWProtocolWebSocket.Metadata(opcode: .text)]
        )
        client.send(content: Data(text.utf8), contentContext: context, isComplete: true, completion: .idempotent)
    }
}

import Foundation

public enum Direction: Int, Sendable {
    case negative = 255
    case stopped = 0
    case positive = 1
}

public enum DaemonCommand: Equatable, Sendable {
    case panTilt(panDirection: Direction, panSpeed: Int, tiltDirection: Direction, tiltSpeed: Int)
    case zoom(Int)
    case stop, center, status
    case settingsGet
    case settingSet(id: String, value: Int)

    public func jsonString() throws -> String {
        String(decoding: try jsonData(), as: UTF8.self)
    }

    public func jsonData() throws -> Data {
        var object: [String: Any] = ["cmd": name]
        switch self {
        case let .panTilt(panDirection, panSpeed, tiltDirection, tiltSpeed):
            object["pan_dir"] = panDirection.rawValue
            object["pan_speed"] = max(0, min(30, panSpeed))
            object["tilt_dir"] = tiltDirection.rawValue
            object["tilt_speed"] = max(0, min(20, tiltSpeed))
        case let .zoom(value): object["value"] = max(100, min(400, value))
        case let .settingSet(id, value):
            object["id"] = id
            object["value"] = value
        case .stop, .center, .status, .settingsGet: break
        }
        return try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
    }

    private var name: String {
        switch self {
        case .panTilt: "pan_tilt"
        case .zoom: "zoom"
        case .stop: "stop"
        case .center: "center"
        case .status: "status"
        case .settingsGet: "settings_get"
        case .settingSet: "setting_set"
        }
    }
}

public enum DaemonReply: Equatable, Sendable {
    case status(cameraConnected: Bool, zoom: Int)
    case acknowledgement
    case error(String)
    case settings([CameraSetting])
    case settingsError(String)

    public static func decode(_ data: Data) throws -> DaemonReply {
        let envelope: Envelope
        do { envelope = try JSONDecoder().decode(Envelope.self, from: data) }
        catch { throw ProtocolError.malformedReply }
        switch envelope.type {
        case "status":
            guard let status = try? JSONDecoder().decode(StatusPayload.self, from: data) else {
                throw ProtocolError.malformedReply
            }
            return .status(cameraConnected: status.camera_connected, zoom: status.zoom)
        case "ack": return .acknowledgement
        case "error":
            let payload = try? JSONDecoder().decode(ErrorPayload.self, from: data)
            return .error(payload?.message ?? "Daemon reported an unknown error")
        case "settings":
            do {
                let snapshot = try JSONDecoder().decode(SettingsSnapshot.self, from: data)
                try snapshot.validate()
                return .settings(snapshot.controls)
            } catch {
                // An invalid settings payload must not take down camera motion.
                return .settingsError("Invalid camera settings response. Choose Refresh to try again.")
            }
        case "settings_error":
            let payload = try? JSONDecoder().decode(ErrorPayload.self, from: data)
            return .settingsError(payload?.message ?? "Camera settings are unavailable")
        default: throw ProtocolError.unknownReply(envelope.type)
        }
    }

    private struct Envelope: Decodable {
        let type: String
    }

    private struct StatusPayload: Decodable {
        let camera_connected: Bool
        let zoom: Int
    }

    private struct ErrorPayload: Decodable {
        let message: String?
    }

    public enum ProtocolError: LocalizedError, Equatable {
        case malformedReply, unknownReply(String)

        public var errorDescription: String? {
            switch self {
            case .malformedReply: "Malformed daemon response"
            case let .unknownReply(type): "Unknown daemon response: \(type)"
            }
        }
    }
}

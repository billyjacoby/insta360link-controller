import Foundation

/// The complete public automation surface. No command strings or endpoints pass through.
public enum AutomationAction: Equatable, Sendable {
    public enum Jog: String, CaseIterable, Sendable { case up, down, left, right }
    public enum Mode: String, CaseIterable, Sendable {
        case normal, tracking, whiteboard, overhead, deskview
        public var value: Int {
            switch self { case .normal: 0; case .tracking: 1; case .whiteboard: 4; case .overhead: 5; case .deskview: 6 }
        }
    }
    case center, stop, jog(Jog), zoom(Int), mode(Mode), setting(id: String, value: Int)
    case startPreview, stopPreview, dashboard, refresh

    public static let imageSettingIDs: Set<String> = [
        "brightness", "contrast", "hue", "saturation", "sharpness", "gamma", "gain",
        "backlight_compensation", "power_line_frequency", "white_balance_temperature",
        "white_balance_auto", "focus_absolute", "focus_auto", "exposure_mode",
        "exposure_absolute", "exposure_auto_priority"
    ]

    public struct InvalidURL: LocalizedError {
        public var errorDescription: String? { "Invalid automation URL. Use a supported linkcontrol://action route with exactly its documented parameters." }
    }

    public static func parse(_ url: URL) throws -> Self {
        guard let parts = URLComponents(url: url, resolvingAgainstBaseURL: false),
              parts.scheme == "linkcontrol", parts.host == "action",
              parts.user == nil, parts.password == nil, parts.port == nil, parts.fragment == nil,
              url.absoluteString.hasPrefix("linkcontrol://action/"),
              !url.absoluteString.contains("%"), !url.absoluteString.contains("\\") else { throw InvalidURL() }
        let items = parts.queryItems ?? []
        guard Set(items.map(\.name)).count == items.count,
              items.allSatisfy({ $0.value != nil && !$0.value!.isEmpty }),
              parts.query != "" else { throw InvalidURL() }
        let params = Dictionary(uniqueKeysWithValues: items.map { ($0.name, $0.value!) })
        func integer(_ key: String) throws -> Int {
            guard let raw = params[key], let value = Int(raw), String(value) == raw else { throw InvalidURL() }
            return value
        }
        switch (parts.path, Set(params.keys)) {
        case ("/center", []): return .center
        case ("/stop", []): return .stop
        case ("/start-preview", []): return .startPreview
        case ("/stop-preview", []): return .stopPreview
        case ("/dashboard", []): return .dashboard
        case ("/refresh", []): return .refresh
        case ("/jog", ["direction"]):
            guard let direction = Jog(rawValue: params["direction"]!) else { throw InvalidURL() }
            return .jog(direction)
        case ("/zoom", ["value"]):
            let value = try integer("value")
            guard (100...400).contains(value) else { throw InvalidURL() }
            return .zoom(value)
        case ("/mode", ["value"]):
            guard let mode = Mode(rawValue: params["value"]!) else { throw InvalidURL() }
            return .mode(mode)
        case ("/setting", ["id", "value"]):
            guard imageSettingIDs.contains(params["id"]!) else { throw InvalidURL() }
            return .setting(id: params["id"]!, value: try integer("value"))
        default: throw InvalidURL()
        }
    }

    public var isManualMove: Bool {
        switch self { case .center, .jog, .zoom: true; default: false }
    }

    public var label: String {
        switch self {
        case .center: "Center"
        case .stop: "Stop manual motion"
        case let .jog(direction): "Jog \(direction.rawValue)"
        case let .zoom(value): "Zoom \(value)%"
        case let .mode(mode): "Mode \(mode.rawValue)"
        case let .setting(id, value): "\(id) = \(value)"
        case .startPreview: "Start preview"
        case .stopPreview: "Stop preview"
        case .dashboard: "Open dashboard"
        case .refresh: "Refresh"
        }
    }
}

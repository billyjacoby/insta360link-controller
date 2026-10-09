import Foundation

/// A control advertised by the camera, never a fabricated capability.
public struct CameraSetting: Decodable, Equatable, Identifiable, Sendable {
    public enum Group: String, Decodable, CaseIterable, Sendable {
        case ai, image, whiteBalance = "white_balance", focus, exposure

        public var title: String {
            switch self {
            case .ai: "AI tracking & camera modes"
            case .image: "Image"
            case .whiteBalance: "White balance"
            case .focus: "Focus"
            case .exposure: "Exposure"
            }
        }
    }

    public enum Kind: String, Decodable, Sendable { case integer, boolean, enumeration = "enum" }

    public struct Option: Decodable, Equatable, Identifiable, Sendable {
        public let value: Int
        public let label: String
        public var id: Int { value }
    }

    public let id: String
    public let label: String
    public let group: Group
    public let kind: Kind
    public let value: Int?
    public let min: Int
    public let max: Int
    public let step: Int
    public let defaultValue: Int?
    public let readable: Bool
    public let writable: Bool
    public let active: Bool
    public let unit: String
    public let options: [Option]
    public let error: String?
    public let detail: String?
    public let pending: Bool?

    /// Older helpers omit pending; 255 always means the firmware is transitioning.
    public var isPending: Bool { id == "ai_mode" && (value == 255 || (value != 0 && pending == true)) }

    private enum CodingKeys: String, CodingKey {
        case id, label, group, kind, value, min, max, step, readable, writable, active, unit, options, error, detail, pending
        case defaultValue = "default"
    }

    public var canEdit: Bool { writable && active }
    public var canReset: Bool { canEdit && defaultValue != nil }

    public func formattedValue(_ value: Int?) -> String {
        guard let value else { return "Unavailable" }
        if id == "ai_mode" && value == 255 { return "Switching…" }
        if let option = options.first(where: { $0.value == value }) { return option.label }
        if kind == .boolean && (value == 0 || value == 1) { return value == 0 ? "Off" : "On" }
        return unit.isEmpty ? String(value) : "\(value) \(unit)"
    }

    public func accepts(_ value: Int) -> Bool {
        guard step > 0, value >= min, value <= max else { return false }
        switch kind {
        case .integer: return (value - min) % step == 0
        case .boolean: return value == 0 || value == 1
        case .enumeration: return options.contains { $0.value == value }
        }
    }

    /// All wire numbers must be integral and exactly representable by native
    /// slider Doubles. The bound also makes range/step arithmetic overflow-safe.
    func validate() throws {
        let limit = 1 << 52
        guard Self.supportedIDs.contains(id), !label.isEmpty,
              min >= -limit, max <= limit, min <= max, step >= 0, step <= limit,
              !writable || step > 0,
              Set(options.map(\.value)).count == options.count,
              options.allSatisfy({ !$0.label.isEmpty && $0.value >= min && $0.value <= max }) else {
            throw DaemonReply.ProtocolError.malformedReply
        }
        if writable && kind == .boolean && (min != 0 || max != 1 || step != 1) {
            throw DaemonReply.ProtocolError.malformedReply
        }
        if writable && kind == .enumeration && options.isEmpty {
            throw DaemonReply.ProtocolError.malformedReply
        }
        if let value {
            // Some cameras report vendor-specific current values outside their
            // advertised range/options. Preserve these only as read-only data;
            // editable values must still be valid candidates for this control.
            guard value >= -limit, value <= limit, !writable || accepts(value) else {
                throw DaemonReply.ProtocolError.malformedReply
            }
        }
        if let defaultValue, !accepts(defaultValue) { throw DaemonReply.ProtocolError.malformedReply }
    }

    private static let supportedIDs: Set<String> = [
        "brightness", "contrast", "hue", "saturation", "sharpness", "gamma", "gain",
        "backlight_compensation", "power_line_frequency", "white_balance_temperature",
        "white_balance_auto", "focus_absolute", "focus_auto", "exposure_mode",
        "exposure_absolute", "exposure_auto_priority", "ai_mode"
    ]
}

struct SettingsSnapshot: Decodable {
    let controls: [CameraSetting]

    func validate() throws {
        guard Set(controls.map(\.id)).count == controls.count else {
            throw DaemonReply.ProtocolError.malformedReply
        }
        try controls.forEach { try $0.validate() }
    }
}

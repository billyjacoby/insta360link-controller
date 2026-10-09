import Foundation

enum CameraSettingsFixtures {
    /// Representative ten-control Pro 2 snapshot, including the observed
    /// vendor-specific power-line value and automatic-mode write restrictions.
    static func link2Pro() throws -> String {
        var controls = [
            control("brightness", "Brightness", value: 50),
            control("contrast", "Contrast", value: 50),
            control("hue", "Hue", value: 0, min: -15, max: 15),
            control("saturation", "Saturation", value: 50),
            control("sharpness", "Sharpness", value: 50),
            control("white_balance_temperature", "Temperature", group: "white_balance", value: 5430,
                    min: 2000, max: 10000, writable: false, active: false, unit: "K"),
            control("white_balance_auto", "Auto white balance", group: "white_balance", kind: "boolean", value: 1, max: 1),
            control("focus_absolute", "Focus", group: "focus", value: 79, writable: false, active: false),
            control("focus_auto", "Autofocus", group: "focus", kind: "boolean", value: 1, max: 1),
            control("power_line_frequency", "Power line frequency", kind: "enum", value: 3, max: 2, writable: false)
        ]
        controls[9]["options"] = [
            ["value": 0, "label": "Disabled"],
            ["value": 1, "label": "50 Hz"],
            ["value": 2, "label": "60 Hz"]
        ]
        controls[9]["default"] = NSNull()
        controls[9]["error"] = "Camera reports nonstandard power-line value 3; writing is disabled."
        return String(decoding: try JSONSerialization.data(withJSONObject: ["type": "settings", "controls": controls]), as: UTF8.self)
    }

    private static func control(
        _ id: String, _ label: String, group: String = "image", kind: String = "integer", value: Int,
        min: Int = 0, max: Int = 100, writable: Bool = true, active: Bool = true, unit: String = ""
    ) -> [String: Any] {
        ["id": id, "label": label, "group": group, "kind": kind, "value": value,
         "min": min, "max": max, "step": 1, "default": value, "readable": true,
         "writable": writable, "active": active, "unit": unit, "options": [], "error": NSNull()]
    }
}

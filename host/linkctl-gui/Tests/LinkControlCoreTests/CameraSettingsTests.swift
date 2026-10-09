import XCTest
@testable import LinkControlCore
@testable import LinkControlGUI

final class CameraSettingsTests: XCTestCase {
    private func decodeControls(_ text: String = MockDaemon.defaultSettings) throws -> [CameraSetting] {
        let reply = try DaemonReply.decode(Data(text.utf8))
        guard case let .settings(controls) = reply else {
            XCTFail("Expected settings, got \(reply)")
            throw DaemonReply.ProtocolError.malformedReply
        }
        return controls
    }

    private func payload(_ edit: (inout [[String: Any]]) -> Void) throws -> String {
        var json = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(MockDaemon.defaultSettings.utf8)) as? [String: Any])
        var controls = try XCTUnwrap(json["controls"] as? [[String: Any]])
        edit(&controls)
        json["controls"] = controls
        return String(decoding: try JSONSerialization.data(withJSONObject: json), as: UTF8.self)
    }

    func testSettingsCommandsEncodeExactWireSchemaWithoutPTZClamping() throws {
        XCTAssertEqual(try DaemonCommand.settingsGet.jsonString(), #"{"cmd":"settings_get"}"#)
        XCTAssertEqual(try DaemonCommand.settingSet(id: "brightness", value: -32).jsonString(),
                       #"{"cmd":"setting_set","id":"brightness","value":-32}"#)
        XCTAssertEqual(try DaemonCommand.settingSet(id: "white_balance_temperature", value: 6500).jsonString(),
                       #"{"cmd":"setting_set","id":"white_balance_temperature","value":6500}"#)
    }

    func testAIModeUsesProEnumAndReadOnlyTransitionState() throws {
        let mode = try XCTUnwrap(decodeControls(AIModeFixtures.snapshot(6)).first)
        XCTAssertEqual(mode.group, .ai)
        XCTAssertTrue(mode.canEdit)
        XCTAssertFalse(mode.canReset, "Never invent a vendor-control default")
        XCTAssertEqual(mode.formattedValue(mode.value), "DeskView")
        for value in [0, 1, 4, 5, 6] { XCTAssertTrue(mode.accepts(value)) }
        for value in [2, 3, 255] { XCTAssertFalse(mode.accepts(value)) }
        XCTAssertEqual(try DaemonCommand.settingSet(id: mode.id, value: 6).jsonString(),
                       #"{"cmd":"setting_set","id":"ai_mode","value":6}"#)
        let transitioning = try XCTUnwrap(decodeControls(AIModeFixtures.snapshot(255)).first)
        XCTAssertFalse(transitioning.canEdit)
        XCTAssertTrue(transitioning.isPending)
        XCTAssertEqual(transitioning.formattedValue(255), "Switching…")
        let invalid = AIModeFixtures.snapshot(255).replacingOccurrences(of: "\"writable\":false", with: "\"writable\":true")
        guard case .settingsError = try DaemonReply.decode(Data(invalid.utf8)) else {
            return XCTFail("255 is never a writable enum value")
        }
    }

    func testAIModePendingIsOptionalAndNormalIgnoresStaleWhiteboardSubstate() throws {
        let normal = try XCTUnwrap(decodeControls(AIModeFixtures.snapshot(0, pending: true)).first)
        XCTAssertFalse(normal.isPending)
        let detecting = try XCTUnwrap(decodeControls(AIModeFixtures.snapshot(4, pending: true)).first)
        XCTAssertTrue(detecting.isPending)
        XCTAssertNotNil(detecting.detail)
        let legacy = AIModeFixtures.snapshot(1).replacingOccurrences(of: "\"pending\":false,", with: "")
        XCTAssertNil(try decodeControls(legacy).first?.pending)
        XCTAssertFalse(try XCTUnwrap(decodeControls(legacy).first).isPending)
    }

    func testCapabilitiesDisplayOnlyAdvertisedGroupsAndHonorAutoDependencies() throws {
        let controls = try decodeControls()
        XCTAssertEqual(controls.map(\.id), ["brightness", "focus_auto", "focus_absolute"])
        XCTAssertEqual(Set(controls.map(\.group)), [.image, .focus])
        XCTAssertFalse(controls.contains { $0.group == .exposure })
        XCTAssertTrue(controls[0].canEdit)
        XCTAssertTrue(controls[0].canReset)
        XCTAssertEqual(controls[0].min, -64)
        XCTAssertEqual(controls[0].max, 64)
        XCTAssertEqual(controls[1].formattedValue(controls[1].value), "On")
        XCTAssertFalse(controls[2].canEdit)
        XCTAssertFalse(controls[2].canReset)
    }

    func testNullValuesErrorsUnitsAndReadOnlySingleValueRangeArePreserved() throws {
        let text = try payload {
            $0[0]["value"] = NSNull()
            $0[0]["default"] = NSNull()
            $0[0]["readable"] = false
            $0[0]["writable"] = false
            $0[0]["min"] = 8
            $0[0]["max"] = 8
            $0[0]["unit"] = "units"
            $0[0]["error"] = "Device read failed"
        }
        let control = try XCTUnwrap(decodeControls(text).first)
        XCTAssertNil(control.value)
        XCTAssertNil(control.defaultValue)
        XCTAssertFalse(control.canReset)
        XCTAssertFalse(control.canEdit)
        XCTAssertEqual(control.formattedValue(nil), "Unavailable")
        XCTAssertEqual(control.formattedValue(8), "8 units")
        XCTAssertEqual(control.error, "Device read failed")
        XCTAssertTrue(control.accepts(8))
    }

    func testEnumUsesAdvertisedOptionsIncludingNoncontiguousExposureModes() throws {
        let text = try payload {
            $0[0].merge([
                "id": "exposure_mode", "label": "Exposure mode", "group": "exposure", "kind": "enum",
                "min": 1, "max": 8, "value": 8, "default": 2,
                "options": [["value": 1, "label": "Manual"], ["value": 2, "label": "Auto"], ["value": 8, "label": "Aperture priority"]]
            ]) { _, new in new }
        }
        let control = try XCTUnwrap(decodeControls(text).first)
        XCTAssertEqual(control.formattedValue(8), "Aperture priority")
        XCTAssertTrue(control.accepts(2))
        XCTAssertFalse(control.accepts(3))
    }

    func testInvalidRangesStepsValuesAndDuplicateIdentitiesAreNonfatalSettingsErrors() throws {
        let edits: [(inout [[String: Any]]) -> Void] = [
            { $0[0]["min"] = 65 },
            { $0[0]["step"] = 0 },
            { $0[0]["step"] = -1 },
            { $0[0]["value"] = 0.5 },
            { $0[0]["value"] = true },
            { $0[0]["value"] = 65 },
            { $0[0]["default"] = -65 },
            { $0[0]["max"] = Int.max },
            { $0[0]["min"] = Int.min },
            { $0[0]["step"] = 3; $0[0]["value"] = 0 },
            { $0[0]["kind"] = "enum" },
            { $0[0]["kind"] = "enum"; $0[0]["options"] = [["value": 0, "label": "A"], ["value": 0, "label": "B"]] },
            { $0[0]["kind"] = "enum"; $0[0]["options"] = [["value": 999, "label": "A"]] },
            { $0[1]["value"] = 2 },
            { $0[1]["min"] = -1 },
            { $0[0]["id"] = "tracking" },
            { $0[0]["group"] = "unknown" },
            { $0[0]["kind"] = "float" },
            { $0.append($0[0]) }
        ]
        for edit in edits {
            let text = try payload(edit)
            guard case .settingsError = try DaemonReply.decode(Data(text.utf8)) else {
                return XCTFail("Invalid settings should fail in isolation: \(text)")
            }
        }
        let overflow = MockDaemon.defaultSettings.replacingOccurrences(of: "\"min\":-64", with: "\"min\":1e999")
        guard case .settingsError = try DaemonReply.decode(Data(overflow.utf8)) else {
            return XCTFail("Non-finite settings numbers must be rejected")
        }
    }

    func testSettingsErrorsAndEmptyCapabilitiesDecodeWithoutInventingControls() throws {
        XCTAssertEqual(try DaemonReply.decode(Data(#"{"type":"settings_error","message":"USB read failed"}"#.utf8)),
                       .settingsError("USB read failed"))
        XCTAssertEqual(try decodeControls(#"{"type":"settings","controls":[]}"#), [])
        guard case .settingsError = try DaemonReply.decode(Data(#"{"type":"settings_error","message":12}"#.utf8)) else {
            return XCTFail("Malformed settings error must remain nonfatal")
        }
    }

    func testNonstandardReadOnlyEnumPreservesAllTenAdvertisedPro2Controls() throws {
        let controls = try decodeControls(CameraSettingsFixtures.link2Pro())
        XCTAssertEqual(controls.count, 10)
        XCTAssertEqual(Set(controls.map(\.group)), [.image, .whiteBalance, .focus])
        let frequency = try XCTUnwrap(controls.first { $0.id == "power_line_frequency" })
        XCTAssertEqual(frequency.value, 3)
        XCTAssertEqual(frequency.options.map(\.value), [0, 1, 2])
        XCTAssertEqual(frequency.formattedValue(frequency.value), "3")
        XCTAssertNotNil(frequency.error)
        XCTAssertNil(frequency.defaultValue)
        XCTAssertFalse(frequency.canEdit)
        XCTAssertFalse(frequency.canReset)
        XCTAssertFalse(frequency.accepts(3), "Preserving a device report must not authorize it as a write candidate")
        XCTAssertTrue(frequency.accepts(2))
        XCTAssertEqual(controls.first { $0.id == "brightness" }?.value, 50)
        XCTAssertEqual(controls.first { $0.id == "white_balance_temperature" }?.value, 5430)
        XCTAssertEqual(controls.first { $0.id == "focus_absolute" }?.value, 79)
    }

    func testReadOnlyIntegerAndUnknownBooleanDisplayRawValues() throws {
        let text = try payload {
            $0[0]["value"] = 1000
            $0[0]["writable"] = false
            $0[0]["unit"] = "units"
            $0[1]["value"] = 2
            $0[1]["writable"] = false
        }
        let controls = try decodeControls(text)
        XCTAssertEqual(controls[0].formattedValue(controls[0].value), "1000 units")
        XCTAssertFalse(controls[0].accepts(1000))
        XCTAssertFalse(controls[0].canEdit)
        XCTAssertEqual(controls[1].formattedValue(controls[1].value), "2", "An unknown boolean must not become On")
        XCTAssertFalse(controls[1].accepts(2))
        XCTAssertFalse(controls[1].canEdit)
    }

    func testReadOnlyOutliersStillRequireFiniteIntegralExactlyBoundedValues() throws {
        for value: Any in [Int.max, Int.min, (1 << 52) + 1, -(1 << 52) - 1, 3.5, true, "NaN"] {
            let text = try payload { $0[0]["value"] = value; $0[0]["writable"] = false }
            guard case .settingsError = try DaemonReply.decode(Data(text.utf8)) else {
                return XCTFail("Unsafe read-only report must be rejected: \(value)")
            }
        }
        let valid = try payload { $0[0]["value"] = 999; $0[0]["writable"] = false }
        let nonfinite = valid.replacingOccurrences(of: "\"value\":999", with: "\"value\":1e999")
        guard case .settingsError = try DaemonReply.decode(Data(nonfinite.utf8)) else {
            return XCTFail("Non-finite read-only reports must be rejected")
        }
    }

    func testNonstandardCurrentValueStillRejectedIfBackendMarksItWritable() throws {
        let text = try CameraSettingsFixtures.link2Pro()
        var json = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(text.utf8)) as? [String: Any])
        var controls = try XCTUnwrap(json["controls"] as? [[String: Any]])
        let index = try XCTUnwrap(controls.firstIndex { $0["id"] as? String == "power_line_frequency" })
        controls[index]["writable"] = true
        controls[index]["active"] = false // Inactive alone does not make the wire value safe.
        json["controls"] = controls
        guard case .settingsError = try DaemonReply.decode(JSONSerialization.data(withJSONObject: json)) else {
            return XCTFail("Invalid writable enum value must remain rejected")
        }
    }

    func testReadOnlyUnavailableMetadataDoesNotInvalidateOtherControlsOrCreateWriteCandidates() throws {
        for kind in ["integer", "boolean", "enum"] {
            let text = try payload {
                $0[0]["kind"] = kind
                $0[0]["min"] = 0
                $0[0]["max"] = 0
                $0[0]["step"] = 0
                $0[0]["value"] = NSNull()
                $0[0]["default"] = NSNull()
                $0[0]["writable"] = false
                $0[0]["readable"] = false
                $0[0]["options"] = []
                $0[0]["error"] = "GET_INFO or range unavailable"
            }
            let controls = try decodeControls(text)
            XCTAssertEqual(controls.count, 3)
            let unavailable = controls[0]
            XCTAssertEqual(unavailable.formattedValue(unavailable.value), "Unavailable")
            XCTAssertEqual(unavailable.error, "GET_INFO or range unavailable")
            XCTAssertFalse(unavailable.canEdit)
            XCTAssertFalse(unavailable.canReset)
            XCTAssertFalse(unavailable.accepts(0), "Zero step must never become a write candidate or divide by zero")
            XCTAssertTrue(controls[1].canEdit)

            // A missing range can coexist with a successfully read current value.
            let withCurrent = text.replacingOccurrences(of: "\"value\":null", with: "\"value\":79")
            let reported = try XCTUnwrap(decodeControls(withCurrent).first)
            XCTAssertEqual(reported.formattedValue(reported.value), "79")
            XCTAssertFalse(reported.accepts(79))
        }
    }

    func testUnavailableMetadataStillRejectedForWritableControls() throws {
        for kind in ["integer", "boolean", "enum"] {
            let text = try payload {
                $0[0]["kind"] = kind
                $0[0]["min"] = 0
                $0[0]["max"] = 0
                $0[0]["step"] = 0
                $0[0]["value"] = NSNull()
                $0[0]["default"] = NSNull()
                $0[0]["options"] = []
            }
            guard case .settingsError = try DaemonReply.decode(Data(text.utf8)) else {
                return XCTFail("Missing \(kind) metadata must not be accepted for writable controls")
            }
        }
    }

    func testNumericDraftSurvivesOtherSnapshotsUntilApplyAndAdoptsDeviceReadback() throws {
        let original = try XCTUnwrap(decodeControls().first)
        var draft = SettingDraft(control: original)
        draft.edit(23)
        let changed = try XCTUnwrap(decodeControls(payload { $0[0]["value"] = 12 }).first)
        draft.reconcile(changed)
        XCTAssertEqual(draft.value, 23, "Background status/other writes must preserve unsaved edits")
        XCTAssertTrue(draft.isEdited)
        draft.submitted()
        draft.reconcile(changed)
        XCTAssertEqual(draft.value, 12, "After Apply, the device's readback is authoritative")
        XCTAssertFalse(draft.isEdited)
    }

    func testDraftIsRebasedWhenDeviceChangesItsRange() throws {
        let original = try XCTUnwrap(decodeControls().first)
        var draft = SettingDraft(control: original)
        draft.edit(63)
        let changed = try XCTUnwrap(decodeControls(payload { $0[0]["max"] = 32 }).first)
        draft.reconcile(changed)
        XCTAssertEqual(draft.value, 0)
        XCTAssertFalse(draft.isEdited)
    }
}

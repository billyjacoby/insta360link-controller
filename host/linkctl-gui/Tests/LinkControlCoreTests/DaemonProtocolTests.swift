import XCTest
@testable import LinkControlCore

final class DaemonProtocolTests: XCTestCase {
    func testPanTiltUsesDaemonDirectionsAndLimits() throws {
        let data = try DaemonCommand.panTilt(panDirection: .negative, panSpeed: 99, tiltDirection: .positive, tiltSpeed: 99).jsonData()
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        XCTAssertEqual(json["pan_dir"] as? Int, 255); XCTAssertEqual(json["pan_speed"] as? Int, 30)
        XCTAssertEqual(json["tilt_dir"] as? Int, 1); XCTAssertEqual(json["tilt_speed"] as? Int, 20)
    }
    func testZoomIsClampedToSupportedRange() throws {
        let data = try DaemonCommand.zoom(999).jsonData()
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        XCTAssertEqual(json["value"] as? Int, 400)
    }
    func testStatusAndErrorRepliesDecode() throws {
        XCTAssertEqual(try DaemonReply.decode(Data(#"{"type":"status","camera_connected":true,"zoom":150}"#.utf8)), .status(cameraConnected: true, zoom: 150))
        XCTAssertEqual(try DaemonReply.decode(Data(#"{"type":"error","message":"camera not connected"}"#.utf8)), .error("camera not connected"))
    }
    func testMalformedReplyIsRejected() { XCTAssertThrowsError(try DaemonReply.decode(Data(#"{"type":"status"}"#.utf8))) }

    func testStatusRequiresBooleanCameraAndIntegerZoom() {
        for text in [
            #"{"type":"status","camera_connected":1,"zoom":100}"#,
            #"{"type":"status","camera_connected":true,"zoom":true}"#,
            #"{"type":"status","camera_connected":true,"zoom":100.5}"#
        ] {
            XCTAssertThrowsError(try DaemonReply.decode(Data(text.utf8)))
        }
    }

    func testStopCenterAndStatusUseExactTextProtocol() throws {
        XCTAssertEqual(try DaemonCommand.stop.jsonString(), #"{"cmd":"stop"}"#)
        XCTAssertEqual(try DaemonCommand.center.jsonString(), #"{"cmd":"center"}"#)
        XCTAssertEqual(try DaemonCommand.status.jsonString(), #"{"cmd":"status"}"#)
        XCTAssertEqual(try DaemonReply.decode(Data(#"{"type":"ack"}"#.utf8)), .acknowledgement)
    }
}

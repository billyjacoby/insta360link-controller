import XCTest
@testable import LinkControlGUI

final class PreviewDeviceSelectionTests: XCTestCase {
    private let devices = [
        (id: "builtin", name: "FaceTime HD Camera"),
        (id: "link", name: "Insta360 Link 2 Pro")
    ]

    func testInitiallyPrefersInsta360Link() {
        XCTAssertEqual(PreviewDeviceSelection.selectedID(currentID: "", devices: devices), "link")
    }

    func testPreservesUserSelectionWhenLinkIsAvailable() {
        XCTAssertEqual(PreviewDeviceSelection.selectedID(currentID: "builtin", devices: devices), "builtin")
    }

    func testSelectionFallbackAfterDeviceRemovalAndEmptyDiscovery() {
        XCTAssertEqual(PreviewDeviceSelection.selectedID(currentID: "removed", devices: devices), "link")
        XCTAssertEqual(PreviewDeviceSelection.selectedID(currentID: "", devices: [devices[0]]), "builtin")
        XCTAssertEqual(PreviewDeviceSelection.selectedID(currentID: "removed", devices: []), "")
    }
}

import XCTest
@testable import LinkControlGUI

final class DaemonReadinessTests: XCTestCase {
    func testEveryPossibleChunkBoundaryAndNewlineRequirement() {
        let marker = Array("[main] linkctl-daemon running on port 53217\n".utf8)
        for boundary in 0..<marker.count {
            let readiness = DaemonReadiness(port: 53217)
            readiness.append(Data(marker.prefix(boundary)))
            XCTAssertFalse(readiness.isReady)
            readiness.append(Data(marker.dropFirst(boundary)))
            XCTAssertTrue(readiness.isReady)
        }
    }

    func testOnlyExactLineForExpectedPortIsAccepted() {
        let readiness = DaemonReadiness(port: 53217)
        for line in [
            "[main] linkctl-daemon running on port 53218\n",
            "prefix [main] linkctl-daemon running on port 53217\n",
            "[main] linkctl-daemon running on port 532170\n",
            "[main] linkctl-daemon running on port 53217 extra\n",
            String(repeating: "x", count: 100_000),
            "[main] linkctl-daemon running on port 53217\n"
        ] {
            readiness.append(Data(line.utf8))
            XCTAssertFalse(readiness.isReady)
        }
        readiness.append(Data("diagnostic\n[main] linkctl-daemon running on port 53217\nmore output\n".utf8))
        XCTAssertTrue(readiness.isReady)
        XCTAssertFalse(DaemonReadiness(port: 53217).isReady)
    }

    func testConcurrentReadsAndOutputCallbacks() {
        let readiness = DaemonReadiness(port: 53217)
        DispatchQueue.concurrentPerform(iterations: 100) { index in
            if index == 50 {
                readiness.append(Data("[main] linkctl-daemon running on port 53217\n".utf8))
            } else {
                readiness.append(Data("diagnostic\n".utf8))
                _ = readiness.isReady
            }
        }
        XCTAssertTrue(readiness.isReady)
    }
}

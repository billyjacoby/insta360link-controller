import Foundation

/// One launch's stdout readiness signal. FileHandle callbacks and the main actor
/// access this concurrently. Only a complete, exact line for this port counts.
final class DaemonReadiness: @unchecked Sendable {
    private let lock = NSLock()
    private let marker: [UInt8]
    private var line: [UInt8] = []
    private var discardingLine = false
    private var ready = false

    init(port: UInt16) {
        marker = Array("[main] linkctl-daemon running on port \(port)".utf8)
    }

    var isReady: Bool {
        lock.lock()
        defer { lock.unlock() }
        return ready
    }

    func append(_ data: Data) {
        lock.lock()
        defer { lock.unlock() }
        guard !ready else { return }
        for byte in data {
            if byte == 10 {
                if !discardingLine && line == marker { ready = true; return }
                line.removeAll(keepingCapacity: true)
                discardingLine = false
            } else if !discardingLine {
                if line.count < marker.count {
                    line.append(byte)
                } else {
                    // Diagnostic output without a newline cannot grow memory or
                    // make a suffix of a longer line look like the ready marker.
                    line.removeAll(keepingCapacity: true)
                    discardingLine = true
                }
            }
        }
    }
}

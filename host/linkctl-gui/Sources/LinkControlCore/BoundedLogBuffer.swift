import Foundation

/// Pipe callbacks append off-main; the UI samples a bounded snapshot.
public final class BoundedLogBuffer: @unchecked Sendable {
    private let lock = NSLock()
    private let capacity: Int
    private var text = ""

    public init(capacity: Int = 65_536) { self.capacity = max(1, capacity) }

    public func append(_ value: String) {
        lock.lock()
        defer { lock.unlock() }
        text.append(contentsOf: value.suffix(capacity))
        if text.count > capacity { text = String(text.suffix(capacity)) }
    }

    public func snapshot() -> String {
        lock.lock()
        defer { lock.unlock() }
        return text
    }

    public func clear() {
        lock.lock()
        defer { lock.unlock() }
        text = ""
    }
}

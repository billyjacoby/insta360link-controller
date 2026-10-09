import Foundation

/// Submit synchronously from the UI actor. Each operation waits for its predecessor,
/// including across suspension points. A failed command does not break the queue.
@MainActor public final class CommandQueue {
    private var tail: Task<Void, Never>?

    public init() {}

    @discardableResult
    public func submit(_ operation: @escaping @MainActor () async -> Void) -> Task<Void, Never> {
        let predecessor = tail
        let task = Task { @MainActor in
            await predecessor?.value
            await operation()
        }
        tail = task
        return task
    }

    public func drain() async {
        await tail?.value
    }
}

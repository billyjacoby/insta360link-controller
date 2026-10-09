import AVFoundation
import SwiftUI

enum PreviewDeviceSelection {
    static func selectedID(currentID: String, devices: [(id: String, name: String)]) -> String {
        if devices.contains(where: { $0.id == currentID }) { return currentID }
        let link = devices.first {
            $0.name.localizedCaseInsensitiveContains("insta360") && $0.name.localizedCaseInsensitiveContains("link")
        }
        return link?.id ?? devices.first?.id ?? ""
    }
}

/// All session configuration and blocking start/stop calls share one serial queue.
private final class CaptureWorker: @unchecked Sendable {
    let session = AVCaptureSession()
    private let queue = DispatchQueue(label: "LinkControl.capture")

    func start(device: AVCaptureDevice, completion: @escaping @Sendable (Result<Void, Error>) -> Void) {
        queue.async { [self] in
            if session.isRunning { session.stopRunning() }
            do {
                let input = try AVCaptureDeviceInput(device: device)
                session.beginConfiguration()
                session.inputs.forEach { session.removeInput($0) }
                guard session.canAddInput(input) else {
                    session.commitConfiguration()
                    throw PreviewError.cannotAddInput
                }
                session.addInput(input)
                session.commitConfiguration()
                session.startRunning()
                guard session.isRunning else { throw PreviewError.failedToStart }
                completion(.success(()))
            } catch {
                session.beginConfiguration()
                session.inputs.forEach { session.removeInput($0) }
                session.commitConfiguration()
                completion(.failure(error))
            }
        }
    }

    func stop(completion: @escaping @Sendable () -> Void = {}) {
        queue.async { [self] in
            if session.isRunning { session.stopRunning() }
            session.beginConfiguration()
            session.inputs.forEach { session.removeInput($0) }
            session.commitConfiguration()
            completion()
        }
    }

    enum PreviewError: LocalizedError {
        case cannotAddInput, failedToStart
        var errorDescription: String? {
            switch self {
            case .cannotAddInput: "This camera cannot be added to the preview session."
            case .failedToStart: "The camera preview could not start. It may be in use by another app."
            }
        }
    }
}

@MainActor final class PreviewModel: NSObject, ObservableObject {
    @Published private(set) var devices: [AVCaptureDevice] = []
    @Published var selectedID = "" {
        didSet {
            guard selectedID != oldValue, wantsPreview else { return }
            requestAndStart()
        }
    }
    @Published private(set) var isRunning = false
    @Published private(set) var isStarting = false
    @Published private(set) var message = "Preview is off."
    private let worker = CaptureWorker()
    private var generation = UUID()
    private var wantsPreview = false
    private var observers: [NSObjectProtocol] = []
    var session: AVCaptureSession { worker.session }

    override init() {
        super.init()
        refreshDevices()
        for name in [AVCaptureDevice.wasConnectedNotification, AVCaptureDevice.wasDisconnectedNotification] {
            observers.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                Task { @MainActor in self?.refreshDevices() }
            })
        }
        observers.append(NotificationCenter.default.addObserver(
            forName: AVCaptureSession.runtimeErrorNotification, object: worker.session, queue: .main
        ) { [weak self] notification in
            let detail = (notification.userInfo?[AVCaptureSessionErrorKey] as? Error)?.localizedDescription
            Task { @MainActor in
                self?.stop()
                self?.message = detail ?? "Camera preview stopped unexpectedly."
            }
        })
    }

    func refreshDevices() {
        devices = AVCaptureDevice.DiscoverySession(
            deviceTypes: [.builtInWideAngleCamera, .externalUnknown],
            mediaType: .video, position: .unspecified
        ).devices
        guard !devices.contains(where: { $0.uniqueID == selectedID }) else { return }
        if wantsPreview { stop() }
        selectedID = PreviewDeviceSelection.selectedID(
            currentID: selectedID,
            devices: devices.map { (id: $0.uniqueID, name: $0.localizedName) }
        )
        if devices.isEmpty { message = "No preview cameras found." }
    }

    func requestAndStart() {
        generation = UUID()
        let token = generation
        wantsPreview = true
        isStarting = true
        isRunning = false
        message = "Starting preview…"
        // Stop the previous selection even while a permission request is pending.
        worker.stop()
        switch AVCaptureDevice.authorizationStatus(for: .video) {
        case .authorized: start(token: token)
        case .notDetermined:
            AVCaptureDevice.requestAccess(for: .video) { [weak self] granted in
                Task { @MainActor in
                    guard let self, self.generation == token, self.wantsPreview else { return }
                    if granted { self.start(token: token) } else { self.deny() }
                }
            }
        default: deny()
        }
    }

    private func deny() {
        stop()
        message = "Camera access was not granted. Enable it in System Settings to use preview."
    }

    private func start(token: UUID) {
        guard let device = devices.first(where: { $0.uniqueID == selectedID }) else {
            stop()
            message = "Choose a camera first."
            return
        }
        let name = device.localizedName
        worker.start(device: device) { [weak self] result in
            Task { @MainActor in
                guard let self, self.generation == token, self.wantsPreview else { return }
                self.isStarting = false
                switch result {
                case .success:
                    self.isRunning = true
                    self.message = "Live preview from \(name)"
                case let .failure(error):
                    self.isRunning = false
                    self.wantsPreview = false
                    self.message = error.localizedDescription
                }
            }
        }
    }

    func stop() {
        generation = UUID()
        wantsPreview = false
        isStarting = false
        isRunning = false
        message = "Preview is off."
        worker.stop()
    }

    func shutdown() async {
        stop()
        await withCheckedContinuation { continuation in
            worker.stop { continuation.resume() }
        }
    }

    deinit {
        observers.forEach { NotificationCenter.default.removeObserver($0) }
        worker.stop()
    }
}

private final class PreviewView: NSView {
    let previewLayer: AVCaptureVideoPreviewLayer

    init(session: AVCaptureSession) {
        previewLayer = AVCaptureVideoPreviewLayer(session: session)
        super.init(frame: .zero)
        wantsLayer = true
        layer = CALayer()
        layer?.backgroundColor = NSColor.black.cgColor
        previewLayer.videoGravity = .resizeAspect
        layer?.addSublayer(previewLayer)
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    override func layout() {
        super.layout()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        previewLayer.frame = bounds
        CATransaction.commit()
    }
}

struct CameraPreview: NSViewRepresentable {
    let session: AVCaptureSession

    func makeNSView(context: Context) -> NSView { PreviewView(session: session) }
    func updateNSView(_ view: NSView, context: Context) { view.needsLayout = true }
}

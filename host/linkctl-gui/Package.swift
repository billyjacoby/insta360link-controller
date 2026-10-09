// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "LinkControlGUI",
    platforms: [.macOS(.v13)],
    products: [.library(name: "LinkControlCore", targets: ["LinkControlCore"]), .executable(name: "LinkControlGUI", targets: ["LinkControlGUI"])],
    targets: [
        .target(name: "LinkControlCore"),
        .executableTarget(name: "LinkControlGUI", dependencies: ["LinkControlCore"]),
        .testTarget(name: "LinkControlCoreTests", dependencies: ["LinkControlCore", "LinkControlGUI"])
    ]
)

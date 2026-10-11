// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "DubStemMix",
    platforms: [.macOS(.v15)],
    dependencies: [
        // Stem separation (PRD § 12): ONNX Runtime, Microsoft's official SwiftPM package (MIT).
        .package(url: "https://github.com/microsoft/onnxruntime-swift-package-manager", from: "1.19.2"),
    ],
    targets: [
        // Noyaux DSP temps réel des effets intégrés (C : ni allocation ni verrou sur le thread audio).
        .target(name: "DubDSP"),
        // MP3 encoder for exported sessions (PRD § 5.9): libmp3lame 3.100, unmodified, LGPL 2 (see Sources/LAME/COPYING).
        .target(
            name: "LAME",
            exclude: ["COPYING"],
            cSettings: [
                .define("STDC_HEADERS"), .define("HAVE_STDINT_H"), .define("HAVE_ERRNO_H"), .define("HAVE_FCNTL_H"),
                .define("HAVE_LIMITS_H"), .define("ieee754_float32_t", to: "float"), .headerSearchPath("."),
                .unsafeFlags(["-w"]), // third-party code, left as is
            ]
        ),
        // Moteur audio, MIDImix et logique de mix — sans interface, testable.
        .target(name: "DubStemMixCore", dependencies: ["DubDSP", "LAME"]),
        .testTarget(name: "DubStemMixCoreTests", dependencies: ["DubStemMixCore"]),
        // Stem separation with htdemucs_ft (PRD § 12): decoding, overlap-add, ONNX inference, model download.
        .target(
            name: "StemSplit",
            dependencies: [.product(name: "onnxruntime", package: "onnxruntime-swift-package-manager")]
        ),
        .testTarget(name: "StemSplitTests", dependencies: ["StemSplit"]),
        // L'app SwiftUI.
        .executableTarget(
            name: "DubStemMix",
            dependencies: ["DubStemMixCore", "StemSplit"],
            resources: [.process("Resources")]
        ),
    ]
)

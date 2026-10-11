import Accelerate
import AVFoundation
import DubDSP
import Foundation
import LAME

/// Exports a recorded session to MP3 (PRD § 5.9): measures the WAV, applies one gain that brings it to −14 LUFS
/// without its true peak going over −1.5 dBTP (it stays quieter rather than being compressed), and encodes
/// 320 kb/s CBR next to the WAV. Off the main thread; slow on a long session, so it reports progress.
public enum SessionExport {
    public static let targetLoudness = -14.0
    public static let peakCeiling = -1.5
    public static let bitrate: Int32 = 320

    public struct Result: Sendable, Equatable {
        public let loudness: Double  // LUFS of the WAV
        public let truePeak: Double  // dBTP of the WAV
        public let gain: Double      // dB applied
    }

    public enum Failure: LocalizedError {
        case silent, encoder, memory
        public var errorDescription: String? {
            switch self {
            case .silent: "the recording is silent"
            case .encoder: "the MP3 encoder failed"
            case .memory: "out of memory"
            }
        }
    }

    /// The gain (dB) that reaches the target loudness, capped by the peak ceiling.
    public static func gain(loudness: Double, truePeak: Double) -> Double {
        min(targetLoudness - loudness, peakCeiling - truePeak)
    }

    /// `progress` goes from 0 to 1: the first half measures, the second encodes. Throws `CancellationError`
    /// when the task is cancelled; nothing is left behind then.
    public static func exportMP3(from wav: URL, to mp3: URL, title: String,
                                 progress: @Sendable (Double) -> Void) throws -> Result {
        // Pass 1: loudness and true peak.
        let measured = try read(wav, progress: { progress($0 * 0.5) }) { file in
            guard let meter = dub_loudness_create(file.processingFormat.sampleRate) else { throw Failure.memory }
            return meter
        } each: { meter, left, right, frames in
            if dub_loudness_add(meter, left, right, Int32(frames)) == 0 { throw Failure.memory }
        } finish: { meter in
            defer { dub_loudness_destroy(meter) }
            return (dub_loudness_integrated(meter), dub_loudness_true_peak(meter))
        }
        guard measured.0.isFinite, measured.1.isFinite else { throw Failure.silent }
        let result = Result(loudness: measured.0, truePeak: measured.1, gain: gain(loudness: measured.0, truePeak: measured.1))
        let scale = Float(pow(10, result.gain / 20))

        // Pass 2: encode into a temporary file, moved over the old MP3 only once complete.
        let partial = mp3.deletingLastPathComponent().appending(path: ".\(mp3.lastPathComponent).partial")
        FileManager.default.createFile(atPath: partial.path(percentEncoded: false), contents: nil)
        let output = try FileHandle(forWritingTo: partial)
        var finished = false
        defer {
            try? output.close()
            if !finished { try? FileManager.default.removeItem(at: partial) }
        }
        try read(wav, progress: { progress(0.5 + $0 * 0.5) }) { file -> Encoder in
            try Encoder(sampleRate: file.processingFormat.sampleRate, title: title)
        } each: { encoder, left, right, frames in
            try output.write(contentsOf: encoder.encode(left, right, frames: frames, scale: scale))
        } finish: { encoder in
            try output.write(contentsOf: encoder.flush())
        }
        try output.close()
        _ = try? FileManager.default.removeItem(at: mp3)
        try FileManager.default.moveItem(at: partial, to: mp3)
        finished = true
        return result
    }

    /// Reads the WAV in chunks, as non-interleaved float stereo (a mono file is played on both sides).
    private static func read<State, Output>(
        _ url: URL, progress: (Double) -> Void,
        start: (AVAudioFile) throws -> State,
        each: (State, UnsafePointer<Float>, UnsafePointer<Float>, Int) throws -> Void,
        finish: (State) throws -> Output
    ) throws -> Output {
        let file = try AVAudioFile(forReading: url, commonFormat: .pcmFormatFloat32, interleaved: false)
        let chunk: AVAudioFrameCount = 65_536
        guard let buffer = AVAudioPCMBuffer(pcmFormat: file.processingFormat, frameCapacity: chunk) else { throw Failure.memory }
        let state = try start(file)
        let total = max(1, Double(file.length))
        var done = 0.0
        while file.framePosition < file.length { // reading at the end throws instead of returning nothing
            try Task.checkCancellation()
            try file.read(into: buffer, frameCount: chunk)
            let frames = Int(buffer.frameLength)
            guard frames > 0, let channels = buffer.floatChannelData else { break }
            let right = file.processingFormat.channelCount > 1 ? channels[1] : channels[0]
            try each(state, channels[0], right, frames)
            done += Double(frames)
            progress(done / total)
        }
        return try finish(state)
    }

    /// One LAME encoder: stereo, 320 kb/s CBR, the WAV's sample rate, title in the ID3 tag.
    private final class Encoder {
        private let lame: OpaquePointer
        private var output: [UInt8] = []
        private var scaledLeft: [Float] = [], scaledRight: [Float] = []

        init(sampleRate: Double, title: String) throws {
            guard let lame = lame_init() else { throw Failure.encoder }
            self.lame = lame
            lame_set_in_samplerate(lame, Int32(sampleRate))
            lame_set_out_samplerate(lame, Int32(sampleRate))
            lame_set_num_channels(lame, 2)
            lame_set_mode(lame, JOINT_STEREO)
            lame_set_brate(lame, SessionExport.bitrate)
            lame_set_VBR(lame, vbr_off)
            lame_set_quality(lame, 2)
            id3tag_init(lame)
            id3tag_set_title(lame, title)
            guard lame_init_params(lame) >= 0 else {
                lame_close(lame)
                throw Failure.encoder
            }
        }

        deinit { lame_close(lame) }

        func encode(_ left: UnsafePointer<Float>, _ right: UnsafePointer<Float>, frames: Int, scale: Float) throws -> Data {
            if scaledLeft.count < frames {
                scaledLeft = .init(repeating: 0, count: frames)
                scaledRight = .init(repeating: 0, count: frames)
            }
            var scale = scale
            vDSP_vsmul(left, 1, &scale, &scaledLeft, 1, vDSP_Length(frames))
            vDSP_vsmul(right, 1, &scale, &scaledRight, 1, vDSP_Length(frames))
            let capacity = frames * 5 / 4 + 7_200
            if output.count < capacity { output = .init(repeating: 0, count: capacity) }
            let written = lame_encode_buffer_ieee_float(lame, scaledLeft, scaledRight, Int32(frames), &output, Int32(capacity))
            guard written >= 0 else { throw Failure.encoder }
            return Data(output[0..<Int(written)])
        }

        func flush() throws -> Data {
            if output.count < 7_200 { output = .init(repeating: 0, count: 7_200) }
            let written = lame_encode_flush(lame, &output, Int32(output.count))
            guard written >= 0 else { throw Failure.encoder }
            return Data(output[0..<Int(written)])
        }
    }
}

import AVFoundation
import DubDSP
import Foundation
import Testing
@testable import DubStemMixCore

private func temporaryFolder() throws -> URL {
    let folder = FileManager.default.temporaryDirectory.appending(path: "DubStemMixTests-\(UUID().uuidString)")
    try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    return folder
}

private func stereoSine(_ frequency: Double, amplitude: Float, seconds: Double, sampleRate: Double = 48_000,
                        phase: Double = 0) -> [Float] {
    (0..<Int(seconds * sampleRate)).map { amplitude * Float(sin(2 * .pi * frequency * Double($0) / sampleRate + phase)) }
}

private func writeWAV(_ samples: [Float], to url: URL, sampleRate: Double = 48_000) throws {
    let format = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 2)!
    let settings: [String: Any] = [AVFormatIDKey: kAudioFormatLinearPCM, AVSampleRateKey: sampleRate, AVNumberOfChannelsKey: 2,
                                   AVLinearPCMBitDepthKey: 32, AVLinearPCMIsFloatKey: true, AVLinearPCMIsBigEndianKey: false]
    let file = try AVAudioFile(forWriting: url, settings: settings, commonFormat: .pcmFormatFloat32, interleaved: false)
    let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(samples.count))!
    buffer.frameLength = AVAudioFrameCount(samples.count)
    for channel in 0..<2 { samples.withUnsafeBufferPointer { buffer.floatChannelData![channel].update(from: $0.baseAddress!, count: samples.count) } }
    try file.write(from: buffer)
}

private func measure(_ samples: [Float], sampleRate: Double = 48_000) -> (loudness: Double, truePeak: Double) {
    let meter = dub_loudness_create(sampleRate)!
    defer { dub_loudness_destroy(meter) }
    _ = dub_loudness_add(meter, samples, samples, Int32(samples.count))
    return (dub_loudness_integrated(meter), dub_loudness_true_peak(meter))
}

// MARK: - Session folder and sheet

@Test func sessionTimestampsReadLikeChapters() {
    #expect(RecordingSession.timestamp(0) == "00:00")
    #expect(RecordingSession.timestamp(754) == "12:34")
    #expect(RecordingSession.timestamp(3730.9) == "1:02:10")
}

@Test func sessionFoldersAreNamedByTheirStartAndNeverReused() throws {
    let parent = try temporaryFolder()
    defer { try? FileManager.default.removeItem(at: parent) }
    var parts = DateComponents()
    (parts.year, parts.month, parts.day, parts.hour, parts.minute, parts.second) = (2026, 10, 11, 21, 30, 5)
    let date = Calendar.current.date(from: parts)!
    #expect(try RecordingSession.makeFolder(in: parent, startingAt: date).lastPathComponent == "2026-10-11 21h30")
    #expect(try RecordingSession.makeFolder(in: parent, startingAt: date).lastPathComponent == "2026-10-11 21h30m05s")
}

@Test func sessionSheetListsTheSongsWithTheirTimes() throws {
    let parent = try temporaryFolder()
    defer { try? FileManager.default.removeItem(at: parent) }
    let start = Date(timeIntervalSince1970: Date.now.timeIntervalSince1970.rounded(.down)) // the sheet keeps whole seconds
    var session = RecordingSession(started: start, setlist: "SOUND SYSTEM", device: "Scarlett 2i2", sampleRate: 48_000, limiterOn: true)
    session.tracks = [.init(time: 0, title: "WHERE WE BELONG"), .init(time: 754, title: "SECOND SONG")]
    session.duration = 1_200
    session.ended = start.addingTimeInterval(1_200)
    let folder = try RecordingSession.makeFolder(in: parent, startingAt: session.started)
    try session.save(in: folder)
    FileManager.default.createFile(atPath: folder.appending(path: RecordingSession.audioFile).path(percentEncoded: false), contents: Data())

    let text = try String(contentsOf: folder.appending(path: RecordingSession.textFile), encoding: .utf8)
    #expect(text.contains("00:00 WHERE WE BELONG\n12:34 SECOND SONG\n"))
    #expect(text.contains("Setlist: SOUND SYSTEM"))
    #expect(text.contains("Duration: 20:00"))
    #expect(text.contains("Limiter: on\n"))
    #expect(RecordingSession.list(in: parent).map(\.session) == [session])
}

@Test func historyLeavesOutOlderFlatRecordingsAndForeignFolders() throws {
    let parent = try temporaryFolder()
    defer { try? FileManager.default.removeItem(at: parent) }
    FileManager.default.createFile(atPath: parent.appending(path: "Old 2026-09-01 20.00.00.wav").path(percentEncoded: false), contents: Data())
    try FileManager.default.createDirectory(at: parent.appending(path: "Something else"), withIntermediateDirectories: true)
    #expect(RecordingSession.list(in: parent).isEmpty)
}

// MARK: - Loudness

@Test func loudnessMatchesTheEbuReferenceTone() {
    // EBU Tech 3341: 1 kHz at −23 dBFS on both channels reads −23 LUFS.
    let result = measure(stereoSine(1_000, amplitude: Float(pow(10, -23.0 / 20)), seconds: 20))
    #expect(abs(result.loudness - -23) < 0.1)
}

@Test func truePeakFindsThePeakBetweenSamples() {
    // A quarter-rate sine sampled at ±45°: every sample at 0.707, the wave itself reaching 1.
    let samples = stereoSine(12_000, amplitude: 0.5, seconds: 1, phase: .pi / 4)
    #expect(samples.map(abs).max()! < 0.36)
    #expect(abs(measure(samples).truePeak - -6.02) < 0.3)
}

@Test func exportGainStopsAtThePeakCeiling() {
    #expect(SessionExport.gain(loudness: -20, truePeak: -10) == 6)
    #expect(SessionExport.gain(loudness: -20, truePeak: -3) == 1.5)
    #expect(SessionExport.gain(loudness: -8, truePeak: 2.5) == -6)
}

@Test func exportWritesAnMP3AtStreamingLoudness() throws {
    let folder = try temporaryFolder()
    defer { try? FileManager.default.removeItem(at: folder) }
    let wav = folder.appending(path: RecordingSession.audioFile), mp3 = folder.appending(path: RecordingSession.mp3File)
    // A quiet tone with a little noise: −30 LUFS or so, plenty of room under the peak ceiling.
    let samples = stereoSine(220, amplitude: 0.04, seconds: 10).map { $0 + Float.random(in: -0.002...0.002) }
    try writeWAV(samples, to: wav)

    let result = try SessionExport.exportMP3(from: wav, to: mp3, title: "Test", progress: { _ in })
    #expect(result.gain > 10)
    let size = try FileManager.default.attributesOfItem(atPath: mp3.path(percentEncoded: false))[.size] as! Int
    #expect(abs(size - 400_000) < 20_000) // 320 kb/s × 10 s
    #expect(!FileManager.default.fileExists(atPath: folder.appending(path: ".session.mp3.partial").path(percentEncoded: false)))

    let decoded = try AVAudioFile(forReading: mp3, commonFormat: .pcmFormatFloat32, interleaved: false)
    let buffer = AVAudioPCMBuffer(pcmFormat: decoded.processingFormat, frameCapacity: AVAudioFrameCount(decoded.length))!
    try decoded.read(into: buffer)
    let left = Array(UnsafeBufferPointer(start: buffer.floatChannelData![0], count: Int(buffer.frameLength)))
    #expect(abs(measure(left).loudness - SessionExport.targetLoudness) < 0.5)
}

@Test func recordingKeepsWhatGoesOverFullScale() throws {
    let folder = try temporaryFolder()
    defer { try? FileManager.default.removeItem(at: folder) }
    let url = folder.appending(path: RecordingSession.audioFile)
    let format = AVAudioFormat(standardFormatWithSampleRate: 48_000, channels: 2)!
    let recorder = MasterRecorder()
    try recorder.start(to: url, format: format)
    let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: 480)!
    buffer.frameLength = 480
    for channel in 0..<2 { for n in 0..<480 { buffer.floatChannelData![channel][n] = 1.5 } } // +3.5 dBFS
    recorder.write(buffer)
    recorder.stop()

    let file = try AVAudioFile(forReading: url, commonFormat: .pcmFormatFloat32, interleaved: false)
    let read = AVAudioPCMBuffer(pcmFormat: file.processingFormat, frameCapacity: 480)!
    try file.read(into: read)
    #expect(read.floatChannelData![0][100] == 1.5)
}

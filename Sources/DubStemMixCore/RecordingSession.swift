import Foundation

/// One recorded session (PRD § 5.9): its own folder, named by its start, holding `session.wav`, `session.json`
/// (read back by the app), `session.txt` (for people) and `session.mp3` once exported.
public struct RecordingSession: Codable, Equatable, Sendable {
    public struct Track: Codable, Equatable, Sendable {
        /// Seconds into the recording.
        public var time: Double
        public var title: String

        public init(time: Double, title: String) {
            self.time = time
            self.title = title
        }
    }

    public var started: Date
    public var ended: Date?
    /// Seconds of audio in the WAV.
    public var duration: Double
    public var setlist: String?
    public var device: String?
    public var sampleRate: Double
    public var limiterOn: Bool
    /// The limiter was switched on or off during the session.
    public var limiterToggled = false
    public var tracks: [Track] = []

    public init(started: Date, setlist: String?, device: String?, sampleRate: Double, limiterOn: Bool) {
        self.started = started
        self.duration = 0
        self.setlist = setlist
        self.device = device
        self.sampleRate = sampleRate
        self.limiterOn = limiterOn
    }

    public static let audioFile = "session.wav"
    public static let infoFile = "session.json"
    public static let textFile = "session.txt"
    public static let mp3File = "session.mp3"

    // MARK: Folder

    /// `2026-10-11 21h30`, or with the seconds (`2026-10-11 21h30m05s`) when that minute is already taken.
    public static func makeFolder(in parent: URL, startingAt date: Date) throws -> URL {
        let fileManager = FileManager.default
        try fileManager.createDirectory(at: parent, withIntermediateDirectories: true)
        for format in ["yyyy-MM-dd HH'h'mm", "yyyy-MM-dd HH'h'mm'm'ss's'"] {
            let folder = parent.appending(path: stamp(date, format: format), directoryHint: .isDirectory)
            if !fileManager.fileExists(atPath: folder.path(percentEncoded: false)) {
                try fileManager.createDirectory(at: folder, withIntermediateDirectories: false)
                return folder
            }
        }
        throw CocoaError(.fileWriteFileExists)
    }

    private static func stamp(_ date: Date, format: String) -> String {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = format // local time
        return formatter.string(from: date)
    }

    // MARK: Files

    /// Writes `session.json` and `session.txt` (called at the start, at each new song and at the end, so a crash
    /// still leaves the session described).
    public func save(in folder: URL) throws {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        encoder.dateEncodingStrategy = .iso8601
        try encoder.encode(self).write(to: folder.appending(path: Self.infoFile), options: .atomic)
        try text.write(to: folder.appending(path: Self.textFile), atomically: true, encoding: .utf8)
    }

    public static func load(from folder: URL) throws -> RecordingSession {
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        return try decoder.decode(Self.self, from: Data(contentsOf: folder.appending(path: infoFile)))
    }

    /// The session's sheet, as written in `session.txt`.
    public var text: String {
        var lines = ["DubStemMix session", ""]
        lines.append("Date: \(Self.stamp(started, format: "yyyy-MM-dd"))")
        let end = ended.map { " – \(Self.stamp($0, format: "HH:mm:ss"))" } ?? " (not finished)"
        lines.append("Time: \(Self.stamp(started, format: "HH:mm:ss"))\(end)")
        lines.append("Duration: \(Self.timestamp(duration))")
        if let setlist { lines.append("Setlist: \(setlist)") }
        let rate = sampleRate.truncatingRemainder(dividingBy: 1000) == 0
            ? "\(Int(sampleRate / 1000)) kHz" : String(format: "%.1f kHz", sampleRate / 1000)
        lines.append("Audio: \(device.map { "\($0), " } ?? "")\(rate), 32-bit float WAV")
        lines.append("Limiter: \(limiterOn ? "on" : "off")\(limiterToggled ? " (switched during the session)" : "")")
        lines += ["", "Tracklist"]
        lines += tracks.isEmpty ? ["(no song played)"] : tracks.map { "\(Self.timestamp($0.time)) \($0.title)" }
        return lines.joined(separator: "\n") + "\n"
    }

    /// `00:00`, `12:34`, `1:02:10`: what YouTube chapters and Mixcloud tracklists take.
    public static func timestamp(_ seconds: Double) -> String {
        let total = max(0, Int(seconds))
        let (hours, minutes, secs) = (total / 3600, total / 60 % 60, total % 60)
        return hours > 0 ? String(format: "%d:%02d:%02d", hours, minutes, secs) : String(format: "%02d:%02d", minutes, secs)
    }

    // MARK: History

    /// A session folder found on disk.
    public struct Entry: Identifiable, Equatable, Sendable {
        public var id: URL { folder }
        public let folder: URL
        public let session: RecordingSession
        public var audio: URL { folder.appending(path: RecordingSession.audioFile) }
        public var mp3: URL { folder.appending(path: RecordingSession.mp3File) }
        public var hasMP3: Bool { FileManager.default.fileExists(atPath: mp3.path(percentEncoded: false)) }

        public init(folder: URL, session: RecordingSession) {
            self.folder = folder
            self.session = session
        }
    }

    /// The sessions in the recordings folder, newest first. Folders without a readable `session.json` and a WAV
    /// (older flat recordings, anything else) are left out.
    public static func list(in parent: URL) -> [Entry] {
        let folders = (try? FileManager.default.contentsOfDirectory(
            at: parent, includingPropertiesForKeys: [.isDirectoryKey], options: [.skipsHiddenFiles]
        )) ?? []
        return folders.compactMap { folder in
            guard (try? folder.resourceValues(forKeys: [.isDirectoryKey]))?.isDirectory == true,
                  FileManager.default.fileExists(atPath: folder.appending(path: audioFile).path(percentEncoded: false)),
                  let session = try? load(from: folder)
            else { return nil }
            return Entry(folder: folder, session: session)
        }
        .sorted { $0.session.started > $1.session.started }
    }
}

import DubStemMixCore
import Foundation

/// Recorded sessions (PRD § 5.9): one folder per session, the WAV, and a sheet with the tracklist.
extension AppModel {
    func toggleRecording() {
        if isRecording {
            finishRecordingSession()
            return
        }
        do {
            let start = Date.now
            let folder = try RecordingSession.makeFolder(in: recordingsFolder, startingAt: start)
            var session = RecordingSession(
                started: start, setlist: setlistName, device: engine.outputDevice?.name,
                sampleRate: engine.outputSampleRate, limiterOn: limiterOn
            )
            try engine.startRecording(to: folder.appending(path: RecordingSession.audioFile))
            if isPlaying, !title.isEmpty { // the song already playing opens the tracklist at 00:00
                session.tracks.append(.init(time: 0, title: title))
                loggedSongLoad = songLoads
            } else {
                loggedSongLoad = nil
            }
            try? session.save(in: folder)
            recordingSession = session
            recordingFolder = folder
            errorMessage = nil
            recordingTime = 0
            isRecording = true
        } catch {
            errorMessage = "Can't start recording: \(error.localizedDescription)"
        }
    }

    /// Stops the recorder (if still running) and writes the session's final sheet.
    func finishRecordingSession() {
        engine.stopRecording()
        isRecording = false
        guard var session = recordingSession, let folder = recordingFolder else { return }
        session.ended = .now
        session.duration = engine.recorder.duration
        try? session.save(in: folder)
        lastRecording = folder
        recordingSession = nil
        recordingFolder = nil
    }

    /// Each tick while recording: a song loaded since the last entry that starts playing enters the tracklist.
    func followRecordedSong() {
        guard isPlaying, loggedSongLoad != songLoads, !title.isEmpty, var session = recordingSession else { return }
        loggedSongLoad = songLoads
        session.tracks.append(.init(time: engine.recorder.duration, title: title))
        session.duration = engine.recorder.duration
        recordingSession = session
        if let recordingFolder { try? session.save(in: recordingFolder) }
    }

    func noteLimiterToggled() {
        guard recordingSession != nil, let recordingFolder else { return }
        recordingSession?.limiterToggled = true
        try? recordingSession?.save(in: recordingFolder)
    }

    private var setlistName: String? {
        guard let setlist else { return nil }
        if !setlist.name.isEmpty { return setlist.name }
        return setlistURL?.deletingPathExtension().lastPathComponent.uppercased()
    }
}

// MARK: - Recorded Sessions window

extension AppModel {
    func refreshRecordedSessions() {
        guard !isPreview else { return }
        recordedSessions = RecordingSession.list(in: recordingsFolder)
    }

    /// The session being recorded right now: listed, but neither exported nor trashed.
    func isBeingRecorded(_ entry: RecordingSession.Entry) -> Bool {
        recordingFolder?.standardizedFileURL == entry.folder.standardizedFileURL
    }

    /// Normalised MP3 next to the WAV, in the background; a new export replaces the previous MP3.
    func exportMP3(_ entry: RecordingSession.Entry) {
        let folder = entry.folder
        guard sessionExportTasks[folder] == nil, !isBeingRecorded(entry) else { return }
        sessionExports[folder] = 0
        sessionExportErrors[folder] = nil
        let title = "DubStemMix session \(folder.lastPathComponent)"
        sessionExportTasks[folder] = Task.detached(priority: .utility) { [weak self] in
            let outcome: Result<SessionExport.Result, Error>
            do {
                outcome = .success(try SessionExport.exportMP3(from: entry.audio, to: entry.mp3, title: title) { progress in
                    Task { @MainActor in
                        if self?.sessionExports[folder] != nil { self?.sessionExports[folder] = progress }
                    }
                })
            } catch {
                outcome = .failure(error)
            }
            await MainActor.run {
                guard let self else { return }
                self.sessionExportTasks[folder] = nil
                self.sessionExports[folder] = nil
                if case let .failure(error) = outcome, !(error is CancellationError) {
                    self.sessionExportErrors[folder] = "Export failed: \(error.localizedDescription)"
                }
                self.refreshRecordedSessions()
            }
        }
    }

    func cancelExport(_ entry: RecordingSession.Entry) {
        sessionExportTasks[entry.folder]?.cancel()
    }

    func trashSession(_ entry: RecordingSession.Entry) {
        guard !isBeingRecorded(entry) else { return }
        cancelExport(entry)
        do {
            try FileManager.default.trashItem(at: entry.folder, resultingItemURL: nil)
        } catch {
            sessionExportErrors[entry.folder] = "Can't move to the Trash: \(error.localizedDescription)"
        }
        if lastRecording?.standardizedFileURL == entry.folder.standardizedFileURL { lastRecording = nil }
        refreshRecordedSessions()
    }
}

// MARK: - Demo data (screenshots)

extension AppModel {
    func loadPreviewSessions() {
        func session(_ day: Int, _ hour: Int, minutes: Double, setlist: String?, songs: [(Double, String)], limiter: Bool = true) -> RecordingSession.Entry {
            var parts = DateComponents()
            (parts.year, parts.month, parts.day, parts.hour, parts.minute) = (2026, 10, day, hour, 30)
            var session = RecordingSession(started: Calendar.current.date(from: parts)!, setlist: setlist, device: "Scarlett 2i2",
                                           sampleRate: 48_000, limiterOn: limiter)
            session.duration = minutes * 60
            session.tracks = songs.map { .init(time: $0.0 * 60, title: $0.1) }
            return .init(folder: recordingsFolder.appending(path: String(format: "2026-10-%02d %dh30", day, hour)), session: session)
        }
        recordedSessions = [
            session(11, 21, minutes: 72.5, setlist: "SOUND SYSTEM NIGHT", songs: [
                (0, "WHERE WE BELONG"), (6.2, "KING TUBBY MEETS"), (13.8, "BLACK ARK"), (21.1, "SATTA DUB"),
                (29.4, "ROCKERS UPTOWN"), (37.9, "STALAG RIDDIM"), (45.2, "WHERE WE BELONG"), (53.7, "MIDNIGHT DUB"),
            ]),
            session(10, 22, minutes: 18.2, setlist: nil, songs: [(0, "WHERE WE BELONG"), (9.5, "BLACK ARK")], limiter: false),
            session(8, 20, minutes: 4.1, setlist: nil, songs: [(0, "SATTA DUB")]),
        ]
        sessionExports[recordedSessions[1].folder] = 0.42
    }
}

import AppKit
import DubStemMixCore
import SwiftUI

/// Recorded Sessions window (PRD § 5.9): the sessions of the recordings folder, newest first; the chosen one shows
/// its tracklist, and can be exported to MP3, shown in the Finder or moved to the Trash.
struct SessionsView: View {
    var model: AppModel
    @State private var selection: URL?
    @State private var confirmingTrash: RecordingSession.Entry?

    private var selected: RecordingSession.Entry? {
        model.recordedSessions.first { $0.folder == selection } ?? model.recordedSessions.first
    }

    var body: some View {
        HStack(spacing: 0) {
            list
                .frame(width: 280)
            Rectangle().fill(Theme.border).frame(width: 1)
            Group {
                if let selected { detail(selected) } else { empty }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        }
        .frame(minWidth: 760, minHeight: 460)
        .background(Theme.bg)
        .environment(\.colorScheme, .dark)
        .onAppear { model.refreshRecordedSessions() }
        .onChange(of: model.isRecording) { model.refreshRecordedSessions() }
        .confirmationDialog(
            "Move this session to the Trash?",
            isPresented: Binding(get: { confirmingTrash != nil }, set: { if !$0 { confirmingTrash = nil } }),
            presenting: confirmingTrash
        ) { entry in
            Button("Move to Trash", role: .destructive) { model.trashSession(entry) }
        } message: { entry in
            Text("\(entry.folder.lastPathComponent): the WAV, its sheet and its MP3.")
        }
    }

    // MARK: List

    private var list: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("RECORDED SESSIONS")
                .font(Fonts.label(12, weight: 850))
                .tracking(1.4)
                .foregroundStyle(Theme.text)
                .padding(.horizontal, 16)
                .padding(.top, 18)
                .padding(.bottom, 10)
            ScrollView {
                LazyVStack(spacing: 2) {
                    ForEach(model.recordedSessions) { entry in
                        row(entry)
                    }
                }
                .padding(.horizontal, 8)
            }
            Spacer(minLength: 0)
            Button { NSWorkspace.shared.open(model.recordingsFolder) } label: {
                Text(model.recordingsFolder.path(percentEncoded: false))
                    .font(Fonts.mono(9.5))
                    .foregroundStyle(Theme.textDim)
                    .lineLimit(1)
                    .truncationMode(.head)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .help("Open the recordings folder (change it in Settings)")
            .padding(16)
        }
    }

    private func row(_ entry: RecordingSession.Entry) -> some View {
        let session = entry.session
        let isSelected = entry.folder == selected?.folder
        return Button { selection = entry.folder } label: {
            VStack(alignment: .leading, spacing: 3) {
                HStack {
                    Text(entry.folder.lastPathComponent)
                        .font(Fonts.label(13, weight: 750, width: 105))
                        .foregroundStyle(Theme.text)
                    Spacer()
                    status(entry)
                }
                Text("\(RecordingSession.timestamp(session.duration)) · \(songCount(session))")
                    .font(Fonts.mono(10))
                    .foregroundStyle(Theme.textDim)
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 8)
            .background(RoundedRectangle(cornerRadius: 5).fill(isSelected ? Theme.surface : .clear))
            .overlay(RoundedRectangle(cornerRadius: 5).stroke(isSelected ? Theme.border : .clear, lineWidth: 1))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }

    @ViewBuilder
    private func status(_ entry: RecordingSession.Entry) -> some View {
        if model.isBeingRecorded(entry) {
            Text("● REC").font(Fonts.mono(9.5, weight: 700)).foregroundStyle(Theme.rec)
        } else if let progress = model.sessionExports[entry.folder] {
            Text("\(Int(progress * 100)) %").font(Fonts.mono(9.5, weight: 700)).foregroundStyle(Theme.delay)
        } else if entry.hasMP3 {
            Text("MP3").font(Fonts.mono(9.5, weight: 700)).foregroundStyle(Theme.reverb)
        }
    }

    private func songCount(_ session: RecordingSession) -> String {
        session.tracks.count == 1 ? "1 song" : "\(session.tracks.count) songs"
    }

    // MARK: Detail

    private var empty: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("No recorded session yet")
                .font(Fonts.label(15, weight: 800))
                .foregroundStyle(Theme.text)
            Text("Press REC (⌘R) while playing: each session gets its own folder, with the WAV and its tracklist.")
                .font(Fonts.mono(10.5))
                .foregroundStyle(Theme.textDim)
        }
        .padding(26)
    }

    private func detail(_ entry: RecordingSession.Entry) -> some View {
        let session = entry.session
        let recording = model.isBeingRecorded(entry)
        let progress = model.sessionExports[entry.folder]
        return VStack(alignment: .leading, spacing: 18) {
            VStack(alignment: .leading, spacing: 4) {
                Text(entry.folder.lastPathComponent)
                    .font(Fonts.label(20, weight: 850, width: 110))
                    .foregroundStyle(Theme.text)
                Text(summary(session, recording: recording))
                    .font(Fonts.mono(10.5))
                    .foregroundStyle(Theme.textDim)
            }

            HStack(spacing: 8) {
                if let progress {
                    ProgressView(value: progress)
                        .tint(Theme.delay)
                        .frame(width: 160)
                    action("CANCEL", color: Theme.textDim) { model.cancelExport(entry) }
                } else {
                    action(entry.hasMP3 ? "EXPORT MP3 AGAIN" : "EXPORT MP3", color: Theme.delay) { model.exportMP3(entry) }
                        .disabled(recording)
                        .opacity(recording ? 0.4 : 1)
                        .help("320 kb/s MP3 next to the WAV, brought to −14 LUFS (peaks at −1.5 dB at most)")
                }
                action("SHOW IN FINDER", color: Theme.text) {
                    NSWorkspace.shared.activateFileViewerSelecting([entry.hasMP3 ? entry.mp3 : entry.audio])
                }
                Spacer()
                action("MOVE TO TRASH", color: Theme.rec) { confirmingTrash = entry }
                    .disabled(recording)
                    .opacity(recording ? 0.4 : 1)
            }
            if let error = model.sessionExportErrors[entry.folder] {
                Text(error).font(Fonts.mono(10.5)).foregroundStyle(Theme.rec)
            }

            VStack(alignment: .leading, spacing: 8) {
                Text("TRACKLIST")
                    .font(Fonts.label(10.5, weight: 800))
                    .tracking(1)
                    .foregroundStyle(Theme.textDim)
                ScrollView {
                    Text(tracklist(session))
                        .font(Fonts.mono(12))
                        .foregroundStyle(Theme.text)
                        .lineSpacing(4)
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
        }
        .padding(26)
    }

    private func summary(_ session: RecordingSession, recording: Bool) -> String {
        var parts = [recording ? "recording…" : RecordingSession.timestamp(session.duration), songCount(session)]
        if let setlist = session.setlist { parts.append(setlist) }
        parts.append("limiter \(session.limiterOn ? "on" : "off")\(session.limiterToggled ? " (switched)" : "")")
        return parts.joined(separator: " · ")
    }

    private func tracklist(_ session: RecordingSession) -> String {
        session.tracks.isEmpty
            ? "No song played"
            : session.tracks.map { "\(RecordingSession.timestamp($0.time)) \($0.title)" }.joined(separator: "\n")
    }

    private func action(_ title: String, color: Color, perform: @escaping () -> Void) -> some View {
        Button(action: perform) {
            Text(title)
                .font(Fonts.mono(10, weight: 700))
                .foregroundStyle(color)
                .padding(.horizontal, 10)
                .frame(height: 26)
                .overlay(RoundedRectangle(cornerRadius: 4).stroke(Theme.border, lineWidth: 1.5))
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

#pragma once
#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
namespace dubwin {
using Progress = std::function<void(const std::wstring&)>;
std::filesystem::path dataRoot();
void setDataRoot(const std::filesystem::path& folder);
int separationSelfTest();
void createPreviewSong(const std::filesystem::path& folder,int variant);
std::filesystem::path configRoot();
struct SeparationOptions { bool fast=true; int gpu=-1; };
std::vector<std::pair<int,std::wstring>> gpuAdapters();
std::filesystem::path prepareSong(const std::filesystem::path& file, std::atomic<bool>& cancel,
    const Progress& progress, SeparationOptions options);
std::array<std::vector<float>,4> loadPrepared(const std::filesystem::path& folder, std::atomic<bool>& cancel, std::vector<float>* waveform=nullptr);
// Worker thread only. Returns UI channel order: drums, bass, other, vocals, at 48 kHz stereo.
std::array<std::vector<float>,4> separateSong(const std::filesystem::path& file,
    std::atomic<bool>& cancel, const Progress& progress);
}

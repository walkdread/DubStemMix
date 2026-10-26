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
// Worker thread only. Returns UI channel order: vocals, drums, bass, other, at 48 kHz stereo.
std::array<std::vector<float>,4> separateSong(const std::filesystem::path& file,
    std::atomic<bool>& cancel, const Progress& progress);
}

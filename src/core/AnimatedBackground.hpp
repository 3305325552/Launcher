#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace launcher {

std::string animatedBackgroundCacheKey(const std::filesystem::path& source, int fps, int maxWidth, int quality);
std::filesystem::path animatedBackgroundCacheDirectory(const std::filesystem::path& cacheRoot, const std::string& key);
std::vector<std::filesystem::path> animatedBackgroundFrames(const std::filesystem::path& cacheDirectory);
bool generateAnimatedBackgroundCache(const std::filesystem::path& source, const std::filesystem::path& cacheRoot, int fps, int maxWidth,
                                     int quality, std::string* error = nullptr);

} // namespace launcher

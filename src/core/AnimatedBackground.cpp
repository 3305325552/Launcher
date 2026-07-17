#include "core/AnimatedBackground.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <system_error>

namespace launcher {
namespace {

std::uint64_t fnv1a(const std::string& text)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : text) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex64(std::uint64_t value)
{
    char buffer[17]{};
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

std::wstring quote(const std::filesystem::path& path)
{
    std::wstring text = path.wstring();
    std::wstring result = L"\"";
    for (wchar_t ch : text) {
        if (ch == L'"') {
            result += L"\\\"";
        } else {
            result.push_back(ch);
        }
    }
    result += L"\"";
    return result;
}

void setError(std::string* error, const std::string& message)
{
    if (error) {
        *error = message;
    }
}

} // namespace

std::string animatedBackgroundCacheKey(const std::filesystem::path& source, int fps, int maxWidth, int quality)
{
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(source, ec);
    const std::filesystem::path normalized = (ec ? source : absolute).lexically_normal();
    const auto size = std::filesystem::file_size(normalized, ec);
    const std::uintmax_t fileSize = ec ? 0 : size;
    const auto writeTime = std::filesystem::last_write_time(normalized, ec);
    const std::int64_t ticks = ec ? 0 : static_cast<std::int64_t>(writeTime.time_since_epoch().count());
    fps = std::clamp(fps, 1, 30);
    maxWidth = std::clamp(maxWidth, 240, 3840);
    quality = std::clamp(quality, 2, 31);

    std::ostringstream text;
    text << normalized.generic_string() << "|" << fileSize << "|" << ticks << "|" << fps << "|" << maxWidth << "|" << quality;
    return hex64(fnv1a(text.str()));
}

std::filesystem::path animatedBackgroundCacheDirectory(const std::filesystem::path& cacheRoot, const std::string& key)
{
    return cacheRoot / key;
}

std::vector<std::filesystem::path> animatedBackgroundFrames(const std::filesystem::path& cacheDirectory)
{
    std::vector<std::filesystem::path> frames;
    std::error_code ec;
    if (!std::filesystem::exists(cacheDirectory, ec)) {
        return frames;
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(cacheDirectory, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind("frame_", 0) == 0 && entry.path().extension() == ".jpg") {
            frames.push_back(entry.path());
        }
    }
    std::sort(frames.begin(), frames.end());
    return frames;
}

bool generateAnimatedBackgroundCache(const std::filesystem::path& source, const std::filesystem::path& cacheRoot, int fps, int maxWidth,
                                     int quality, std::string* error)
{
    std::error_code ec;
    if (source.empty() || !std::filesystem::exists(source, ec)) {
        setError(error, "source file does not exist");
        return false;
    }

    fps = std::clamp(fps, 1, 30);
    maxWidth = std::clamp(maxWidth, 240, 3840);
    quality = std::clamp(quality, 2, 31);
    const std::string key = animatedBackgroundCacheKey(source, fps, maxWidth, quality);
    const std::filesystem::path directory = animatedBackgroundCacheDirectory(cacheRoot, key);
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        setError(error, ec.message());
        return false;
    }

    const std::filesystem::path outputPattern = directory / "frame_%05d.jpg";
    std::wstring command = L"ffmpeg -y -hide_banner -loglevel error -i " + quote(source) + L" -vf \"fps=" + std::to_wstring(fps) +
                           L",scale=" + std::to_wstring(maxWidth) + L":-2\" -q:v " + std::to_wstring(quality) + L" " + quote(outputPattern);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring mutableCommand = command;
    const BOOL created =
        CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (!created) {
        setError(error, "failed to start ffmpeg; make sure ffmpeg is in PATH");
        return false;
    }

    const DWORD wait = WaitForSingleObject(process.hProcess, 5 * 60 * 1000);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (wait == WAIT_TIMEOUT) {
        setError(error, "ffmpeg timed out");
        return false;
    }
    if (exitCode != 0) {
        setError(error, "ffmpeg failed to generate frames");
        return false;
    }
    if (animatedBackgroundFrames(directory).empty()) {
        setError(error, "ffmpeg generated no frames");
        return false;
    }
    return true;
}

} // namespace launcher

#include "core/LocalVolumeFileProvider.hpp"

#include "core/GlobalFileItem.hpp"
#include "core/StringEncoding.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <utility>

namespace launcher {
namespace {

constexpr const char* kLocalVolumeSignature = "local-fixed-removable-v3-hot-roots";

bool shouldIndexDrive(UINT driveType)
{
    return driveType == DRIVE_FIXED || driveType == DRIVE_REMOVABLE || driveType == DRIVE_RAMDISK;
}

std::vector<std::filesystem::path> localDriveRoots()
{
    std::vector<std::filesystem::path> roots;
    const DWORD mask = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        if ((mask & (1u << (letter - L'A'))) == 0) {
            continue;
        }
        std::wstring root;
        root.push_back(letter);
        root += L":\\";
        if (shouldIndexDrive(GetDriveTypeW(root.c_str()))) {
            roots.emplace_back(root);
        }
    }
    return roots;
}

std::wstring lowerWide(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

std::wstring normalizedWidePath(std::filesystem::path path)
{
    path = path.lexically_normal();
    std::wstring text = lowerWide(path.wstring());
    while (!text.empty() && (text.back() == L'\\' || text.back() == L'/')) {
        text.pop_back();
    }
    return text;
}

bool pathsEqual(const std::filesystem::path& lhs, const std::filesystem::path& rhs)
{
    return normalizedWidePath(lhs) == normalizedWidePath(rhs);
}

bool shouldSkipDirectoryName(const std::filesystem::path& path)
{
    const std::wstring name = lowerWide(path.filename().wstring());
    return name == L"$recycle.bin" || name == L"system volume information" || name == L"$windows.~bt" || name == L"$windows.~ws" ||
           name == L"appdata" || name == L"windows" || name == L"program files" || name == L"program files (x86)" ||
           name == L"programdata" || name == L"node_modules" || name == L".git" || name == L".svn" || name == L".hg" || name == L".cache" ||
           name == L"cache" || name == L"caches" || name == L"tmp" || name == L"temp" || name == L"build" || name == L"dist" ||
           name == L"out" || name == L"target" || name == L"__pycache__" || name == L".gradle" || name == L".idea" || name == L".vs" ||
           name == L".vscode";
}

bool isDirectoryAttributes(DWORD attributes)
{
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool isReparsePoint(DWORD attributes)
{
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

class ScanIoPriorityGuard {
public:
    explicit ScanIoPriorityGuard(GlobalScanPriority priority)
    {
        if (priority == GlobalScanPriority::Normal) {
            belowNormal_ = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL) != 0;
            return;
        }
        backgroundMode_ = SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN) != 0;
        if (!backgroundMode_) {
            belowNormal_ = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL) != 0;
        }
    }

    ~ScanIoPriorityGuard()
    {
        if (backgroundMode_) {
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
        } else if (belowNormal_) {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        }
    }

private:
    bool backgroundMode_ = false;
    bool belowNormal_ = false;
};

std::uint32_t unixSecondsFromFileTime(const FILETIME& fileTime)
{
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    if (value.QuadPart <= 116444736000000000ULL) {
        return 0;
    }
    const unsigned long long seconds = (value.QuadPart - 116444736000000000ULL) / 10000000ULL;
    return static_cast<std::uint32_t>(std::min<unsigned long long>(seconds, std::numeric_limits<std::uint32_t>::max()));
}

std::uint32_t directoryModifiedUnix(const std::filesystem::path& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.wstring().c_str(), GetFileExInfoStandard, &data)) {
        return 0;
    }
    return unixSecondsFromFileTime(data.ftLastWriteTime);
}

void flushBatch(std::vector<GlobalFileRecord>& batch, const GlobalFileProvider::BatchCallback& commitBatch)
{
    if (!batch.empty() && commitBatch) {
        commitBatch(std::move(batch));
        batch.clear();
        batch.reserve(kGlobalFileCommitBatchSize);
    }
}

bool isSkippedRoot(const std::filesystem::path& path, const std::vector<std::filesystem::path>& skipRoots)
{
    for (const std::filesystem::path& skipRoot : skipRoots) {
        if (pathsEqual(path, skipRoot)) {
            return true;
        }
    }
    return false;
}

void scanDirectory(const std::filesystem::path& root, const std::vector<std::filesystem::path>& skipRoots, const std::atomic_bool& stop,
                   std::vector<GlobalFileRecord>& batch, std::vector<GlobalFileRecord>& allItems, bool incrementalCommit, bool collectAll,
                   const GlobalFileProvider::BatchCallback& commitBatch, const LocalVolumeFileProvider::ProgressCallback& onProgress,
                   std::uint64_t& scannedInRoot)
{
    std::vector<std::filesystem::path> stack;
    stack.push_back(root);
    std::uint64_t sinceProgress = 0;

    while (!stack.empty() && !stop.load()) {
        const std::filesystem::path directory = std::move(stack.back());
        stack.pop_back();

        const std::wstring pattern = (directory / L"*").wstring();
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW(pattern.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) {
            continue;
        }

        do {
            if (stop.load()) {
                break;
            }
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") {
                continue;
            }
            const DWORD attributes = data.dwFileAttributes;
            if (isReparsePoint(attributes)) {
                continue;
            }
            const std::filesystem::path path = directory / name;
            const bool directoryEntry = isDirectoryAttributes(attributes);
            if (directoryEntry && (shouldSkipDirectoryName(path) || isSkippedRoot(path, skipRoots))) {
                continue;
            }

            GlobalFileRecord item = makeGlobalFileRecord(path, directoryEntry, unixSecondsFromFileTime(data.ftLastWriteTime));
            ++scannedInRoot;
            ++sinceProgress;
            if (collectAll) {
                allItems.push_back(item);
            }
            if (incrementalCommit) {
                batch.push_back(std::move(item));
            }
            if (incrementalCommit && batch.size() >= kGlobalFileCommitBatchSize) {
                flushBatch(batch, commitBatch);
            }
            if (onProgress && sinceProgress >= 2048) {
                onProgress(scannedInRoot, narrow(path.wstring()));
                sinceProgress = 0;
            }
            if (directoryEntry) {
                stack.push_back(path);
            }
        } while (FindNextFileW(find, &data));

        FindClose(find);
        if (onProgress && sinceProgress >= 256) {
            onProgress(scannedInRoot, narrow(directory.wstring()));
            sinceProgress = 0;
        }
    }
    if (onProgress) {
        onProgress(scannedInRoot, narrow(root.wstring()));
    }
}

std::filesystem::path userProfilePath()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(std::wstring(buffer, length));
}

} // namespace

std::vector<GlobalScanRoot> LocalVolumeFileProvider::enumerateRoots() const
{
    std::vector<GlobalScanRoot> roots;
    const std::filesystem::path profile = userProfilePath();
    if (!profile.empty()) {
        std::error_code ec;
        if (std::filesystem::is_directory(profile, ec)) {
            roots.push_back(GlobalScanRoot{"hot:user-profile", profile, true});
        }
    }

    for (const std::filesystem::path& drive : localDriveRoots()) {
        roots.push_back(GlobalScanRoot{"cold:drive:" + narrow(drive.wstring()), drive, false});
    }
    return roots;
}

std::string LocalVolumeFileProvider::signature() const
{
    std::string value = kLocalVolumeSignature;
    for (const GlobalScanRoot& root : enumerateRoots()) {
        value += "|";
        value += root.id;
        value += "=";
        value += narrow(root.path.wstring());
        value += root.hot ? ":hot" : ":cold";
    }
    return value;
}

std::vector<GlobalFileRecord> LocalVolumeFileProvider::scanRoot(const GlobalScanRoot& root,
                                                                const std::vector<std::filesystem::path>& skipRoots,
                                                                const std::atomic_bool& stop, bool incrementalCommit, bool collectAll,
                                                                BatchCallback commitBatch, GlobalScanPriority priority,
                                                                ProgressCallback onProgress)
{
    ScanIoPriorityGuard priorityGuard(priority);
    std::vector<GlobalFileRecord> allItems;
    if (collectAll) {
        allItems.reserve(root.hot ? 50000 : 200000);
    }
    std::vector<GlobalFileRecord> batch;
    batch.reserve(kGlobalFileCommitBatchSize);
    std::uint64_t scannedInRoot = 0;

    if (!root.path.empty()) {
        std::error_code ec;
        if (std::filesystem::is_directory(root.path, ec)) {
            GlobalFileRecord rootItem = makeGlobalFileRecord(root.path, true, directoryModifiedUnix(root.path));
            ++scannedInRoot;
            if (collectAll) {
                allItems.push_back(rootItem);
            }
            if (incrementalCommit) {
                batch.push_back(std::move(rootItem));
            }
            if (onProgress) {
                onProgress(scannedInRoot, narrow(root.path.wstring()));
            }
            scanDirectory(root.path, skipRoots, stop, batch, allItems, incrementalCommit, collectAll, commitBatch, onProgress,
                          scannedInRoot);
        }
    }

    if (!stop.load() && incrementalCommit) {
        flushBatch(batch, commitBatch);
    }
    if (onProgress) {
        onProgress(scannedInRoot, narrow(root.path.wstring()));
    }
    return allItems;
}

std::vector<GlobalFileRecord> LocalVolumeFileProvider::scan(const std::atomic_bool& stop, bool incrementalCommit, bool collectAll,
                                                            BatchCallback commitBatch)
{
    std::vector<GlobalFileRecord> allItems;
    if (collectAll) {
        allItems.reserve(200000);
    }

    const std::vector<GlobalScanRoot> roots = enumerateRoots();
    std::vector<std::filesystem::path> hotRoots;
    for (const GlobalScanRoot& root : roots) {
        if (root.hot) {
            hotRoots.push_back(root.path);
        }
    }

    for (const GlobalScanRoot& root : roots) {
        if (stop.load()) {
            break;
        }
        std::vector<GlobalFileRecord> rootItems =
            scanRoot(root, root.hot ? std::vector<std::filesystem::path>{} : hotRoots, stop, incrementalCommit, collectAll, commitBatch);
        if (collectAll) {
            allItems.insert(allItems.end(), std::make_move_iterator(rootItems.begin()), std::make_move_iterator(rootItems.end()));
        }
    }
    return allItems;
}

} // namespace launcher

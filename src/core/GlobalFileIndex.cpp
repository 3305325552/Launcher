#include "core/GlobalFileIndex.hpp"

#include "core/GlobalFileItem.hpp"
#include "core/LocalVolumeFileProvider.hpp"
#include "core/StringEncoding.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace launcher {
namespace {

// After cancelling a rebuild/index pass, keep using disk caches for a while.
constexpr auto kCancelRescanCooldown = std::chrono::minutes(30);
constexpr auto kAggressiveBetweenRootPause = std::chrono::milliseconds(20);

struct ScanIntensityProfile {
    std::chrono::milliseconds hotRefresh{};
    std::chrono::milliseconds coldRefresh{};
    std::chrono::milliseconds betweenRootPause{};
    std::chrono::milliseconds idlePoll{};
    std::chrono::milliseconds cacheHotDelay{};
    std::chrono::milliseconds cacheColdDelay{};
    std::chrono::milliseconds missingColdDelay{};
    GlobalScanPriority backgroundPriority = GlobalScanPriority::Background;
};

int clampScanIntensity(int intensity)
{
    return std::clamp(intensity, 0, 2);
}

ScanIntensityProfile intensityProfile(int intensity)
{
    switch (clampScanIntensity(intensity)) {
    case 0: // Low — quiet background updates
        return ScanIntensityProfile{
            .hotRefresh = std::chrono::hours(6),
            .coldRefresh = std::chrono::hours(24 * 7),
            .betweenRootPause = std::chrono::seconds(2),
            .idlePoll = std::chrono::minutes(5),
            .cacheHotDelay = std::chrono::hours(2),
            .cacheColdDelay = std::chrono::hours(24),
            .missingColdDelay = std::chrono::minutes(30),
            .backgroundPriority = GlobalScanPriority::Background,
        };
    case 2: // High — still modest, not foreground full speed
        return ScanIntensityProfile{
            .hotRefresh = std::chrono::minutes(15),
            .coldRefresh = std::chrono::hours(24),
            .betweenRootPause = std::chrono::milliseconds(250),
            .idlePoll = std::chrono::seconds(30),
            .cacheHotDelay = std::chrono::minutes(2),
            .cacheColdDelay = std::chrono::minutes(30),
            .missingColdDelay = std::chrono::seconds(30),
            .backgroundPriority = GlobalScanPriority::Background,
        };
    case 1: // Balanced (default)
    default:
        return ScanIntensityProfile{
            .hotRefresh = std::chrono::hours(1),
            .coldRefresh = std::chrono::hours(48),
            .betweenRootPause = std::chrono::milliseconds(750),
            .idlePoll = std::chrono::minutes(1),
            .cacheHotDelay = std::chrono::minutes(10),
            .cacheColdDelay = std::chrono::hours(6),
            .missingColdDelay = std::chrono::minutes(5),
            .backgroundPriority = GlobalScanPriority::Background,
        };
    }
}

std::string escapeCacheField(const std::string& value)
{
    std::string result;
    result.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '\t': result += "\\t"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        default: result.push_back(ch); break;
        }
    }
    return result;
}

bool unescapeCacheField(const std::string& value, std::string& result)
{
    result.clear();
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\') {
            result.push_back(value[i]);
            continue;
        }
        if (++i >= value.size()) {
            return false;
        }
        switch (value[i]) {
        case '\\': result.push_back('\\'); break;
        case 't': result.push_back('\t'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        default: return false;
        }
    }
    return true;
}

std::uint32_t parseCacheTime(std::string_view value)
{
    unsigned long long parsed = 0;
    for (char ch : value) {
        if (ch < '0' || ch > '9') {
            return 0;
        }
        parsed = parsed * 10 + static_cast<unsigned long long>(ch - '0');
        if (parsed > std::numeric_limits<std::uint32_t>::max()) {
            return std::numeric_limits<std::uint32_t>::max();
        }
    }
    return static_cast<std::uint32_t>(parsed);
}

std::filesystem::path rootsDirectory(const std::filesystem::path& cachePath)
{
    return cachePath.parent_path() / "global-search-roots";
}

std::filesystem::path rootCachePath(const std::filesystem::path& cachePath, const std::string& rootId)
{
    std::string safe = rootId;
    for (char& ch : safe) {
        if (ch == ':' || ch == '/' || ch == '\\' || ch == '|' || ch == '<' || ch == '>' || ch == '?' || ch == '*' || ch == '"') {
            ch = '_';
        }
    }
    return rootsDirectory(cachePath) / (safe + ".tsv");
}

std::filesystem::path cacheMetadataPath(const std::filesystem::path& cachePath)
{
    return cachePath.string() + ".meta";
}

void writeCacheRecord(std::ostream& output, const GlobalFileRecord& item)
{
    output << (item.directory ? 'D' : 'F') << '\t' << item.modifiedTime << '\t' << escapeCacheField(item.path) << '\n';
}

std::string readCacheSignature(const std::filesystem::path& cachePath)
{
    std::ifstream input(cacheMetadataPath(cachePath), std::ios::binary);
    std::string signature;
    std::getline(input, signature);
    return signature;
}

void writeCacheSignature(const std::filesystem::path& cachePath, std::string_view signature)
{
    if (cachePath.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(cachePath.parent_path(), ec);
    if (ec) {
        return;
    }

    std::ofstream output(cacheMetadataPath(cachePath), std::ios::binary);
    output << signature << '\n';
}

bool replaceCacheFile(const std::filesystem::path& tmpPath, const std::filesystem::path& cachePath)
{
    std::error_code ec;
    std::filesystem::rename(tmpPath, cachePath, ec);
    if (!ec) {
        return true;
    }
    std::filesystem::remove(cachePath, ec);
    ec.clear();
    std::filesystem::rename(tmpPath, cachePath, ec);
    return !ec;
}

void saveRootCache(const std::filesystem::path& cachePath, const std::string& rootId, const std::vector<GlobalFileRecord>& files)
{
    if (cachePath.empty() || rootId.empty()) {
        return;
    }

    const std::filesystem::path rootPath = rootCachePath(cachePath, rootId);
    std::error_code ec;
    std::filesystem::create_directories(rootPath.parent_path(), ec);
    if (ec) {
        return;
    }

    const std::filesystem::path tmpPath = rootPath.string() + ".tmp";
    {
        std::ofstream output(tmpPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            return;
        }
        for (const GlobalFileRecord& item : files) {
            writeCacheRecord(output, item);
        }
        if (!output) {
            return;
        }
    }
    replaceCacheFile(tmpPath, rootPath);
}

bool loadRootCache(const std::filesystem::path& cachePath, const std::string& rootId, const std::atomic_bool& stop,
                   std::vector<GlobalFileRecord>& files)
{
    files.clear();
    const std::filesystem::path rootPath = rootCachePath(cachePath, rootId);
    std::ifstream input(rootPath, std::ios::binary);
    if (!input) {
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (stop.load()) {
            files.clear();
            return false;
        }
        if (line.empty()) {
            continue;
        }
        const size_t firstTab = line.find('\t');
        if (firstTab == std::string::npos || firstTab == 0) {
            continue;
        }
        const size_t secondTab = line.find('\t', firstTab + 1);
        if (secondTab == std::string::npos) {
            continue;
        }

        const bool directory = line[0] == 'D';
        const std::uint32_t modifiedTime = parseCacheTime(std::string_view(line).substr(firstTab + 1, secondTab - firstTab - 1));
        std::string pathText;
        if (!unescapeCacheField(line.substr(secondTab + 1), pathText) || pathText.empty()) {
            continue;
        }
        files.push_back(makeGlobalFileRecord(std::move(pathText), directory, modifiedTime));
    }
    return true;
}

bool loadLegacyCache(const std::filesystem::path& cachePath, const std::atomic_bool& stop, const GlobalFileIndex::CommitCallback& commit)
{
    std::ifstream input(cachePath, std::ios::binary);
    if (!input) {
        return false;
    }

    std::vector<GlobalFileRecord> batch;
    batch.reserve(kGlobalFileCommitBatchSize);
    bool any = false;
    std::string line;
    while (std::getline(input, line)) {
        if (stop.load()) {
            return false;
        }
        if (line.empty()) {
            continue;
        }
        const size_t firstTab = line.find('\t');
        if (firstTab == std::string::npos || firstTab == 0) {
            continue;
        }
        const size_t secondTab = line.find('\t', firstTab + 1);
        if (secondTab == std::string::npos) {
            continue;
        }

        const bool directory = line[0] == 'D';
        const std::uint32_t modifiedTime = parseCacheTime(std::string_view(line).substr(firstTab + 1, secondTab - firstTab - 1));
        std::string pathText;
        if (!unescapeCacheField(line.substr(secondTab + 1), pathText) || pathText.empty()) {
            continue;
        }
        batch.push_back(makeGlobalFileRecord(std::move(pathText), directory, modifiedTime));
        any = true;
        if (batch.size() >= kGlobalFileCommitBatchSize) {
            if (commit) {
                commit("legacy", std::move(batch), GlobalFileCommitMode::AppendRoot);
            }
            batch.clear();
            batch.reserve(kGlobalFileCommitBatchSize);
        }
    }
    if (!batch.empty() && commit) {
        commit("legacy", std::move(batch), GlobalFileCommitMode::AppendRoot);
    }
    return any;
}

bool waitForStopOrTimeout(const std::atomic_bool& stop, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!stop.load()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        std::this_thread::sleep_for(std::min(remaining, std::chrono::milliseconds(200)));
    }
    return true;
}

struct RootRuntimeState {
    GlobalScanRoot root;
    std::chrono::steady_clock::time_point nextScan{};
    bool loadedFromCache = false;
    bool scannedOnce = false;
};

std::vector<std::filesystem::path> hotRootPaths(const std::vector<RootRuntimeState>& states)
{
    std::vector<std::filesystem::path> paths;
    for (const RootRuntimeState& state : states) {
        if (state.root.hot) {
            paths.push_back(state.root.path);
        }
    }
    return paths;
}

void commitRootReplace(const GlobalFileIndex::CommitCallback& commit, const std::string& rootId, std::vector<GlobalFileRecord> files)
{
    if (!commit) {
        return;
    }
    commit(rootId, std::move(files), GlobalFileCommitMode::ReplaceRoot);
}

float estimateRootFraction(std::uint64_t scannedInRoot)
{
    // Unknown total size: use smooth asymptotic so bar keeps moving within current root.
    constexpr float kHalfAt = 80000.0f;
    const float x = static_cast<float>(scannedInRoot);
    return std::min(0.97f, x / (x + kHalfAt));
}

bool refreshRoot(LocalVolumeFileProvider& provider, RootRuntimeState& state, const std::vector<std::filesystem::path>& skipRoots,
                 const std::filesystem::path& cachePath, const std::atomic_bool& stop, const GlobalFileIndex::CommitCallback& commit,
                 GlobalScanPriority priority, std::chrono::milliseconds hotRefresh, std::chrono::milliseconds coldRefresh,
                 std::uint64_t* indexedFilesOut,
                 const std::function<void(std::uint64_t scannedInRoot, std::string currentPath)>& onProgress)
{
    if (stop.load()) {
        return false;
    }

    std::vector<GlobalFileRecord> files = provider.scanRoot(state.root, skipRoots, stop, false, true, {}, priority,
                                                            [&](std::uint64_t scannedInRoot, std::string currentPath) {
                                                                if (onProgress) {
                                                                    onProgress(scannedInRoot, std::move(currentPath));
                                                                }
                                                            });
    if (stop.load()) {
        return false;
    }

    if (indexedFilesOut) {
        *indexedFilesOut += files.size();
    }
    saveRootCache(cachePath, state.root.id, files);
    commitRootReplace(commit, state.root.id, std::move(files));
    state.scannedOnce = true;
    state.loadedFromCache = true;
    state.nextScan = std::chrono::steady_clock::now() + (state.root.hot ? hotRefresh : coldRefresh);
    return true;
}

void clearRootCaches(const std::filesystem::path& cachePath)
{
    if (cachePath.empty()) {
        return;
    }
    std::error_code ec;
    const std::filesystem::path rootsDir = rootsDirectory(cachePath);
    if (std::filesystem::exists(rootsDir, ec)) {
        std::filesystem::remove_all(rootsDir, ec);
    }
    std::filesystem::remove(cachePath, ec);
    std::filesystem::remove(cacheMetadataPath(cachePath), ec);
    const std::filesystem::path tmpPath = cachePath.string() + ".tmp";
    std::filesystem::remove(tmpPath, ec);
}

} // namespace

void GlobalFileIndex::setProgress(bool active, bool rebuilding, std::uint32_t completedRoots, std::uint32_t totalRoots,
                                  std::uint64_t indexedFiles, std::uint64_t currentRootFiles, float currentRootFraction,
                                  std::string currentRoot, std::string currentPath, std::string phase)
{
    std::lock_guard lock(progressMutex_);
    progress_.active = active;
    progress_.rebuilding = rebuilding;
    progress_.completedRoots = completedRoots;
    progress_.totalRoots = totalRoots;
    progress_.indexedFiles = indexedFiles;
    progress_.currentRootFiles = currentRootFiles;
    progress_.currentRootFraction = currentRootFraction;
    progress_.currentRoot = std::move(currentRoot);
    progress_.currentPath = std::move(currentPath);
    progress_.phase = std::move(phase);
}

void GlobalFileIndex::clearProgress()
{
    std::lock_guard lock(progressMutex_);
    progress_ = {};
}

GlobalIndexProgress GlobalFileIndex::progress() const
{
    std::lock_guard lock(progressMutex_);
    return progress_;
}

GlobalFileIndex::~GlobalFileIndex()
{
    stop();
}

void GlobalFileIndex::sync(bool enabled, std::filesystem::path cachePath, int scanIntensity, CommitCallback commit)
{
    scanIntensity_.store(clampScanIntensity(scanIntensity));
    LocalVolumeFileProvider provider;
    const std::string desiredSignature =
        enabled ? provider.signature() + "|" + cachePath.string() + "|i" + std::to_string(clampScanIntensity(scanIntensity)) : "";
    if (desiredSignature.empty()) {
        if (!signature_.empty() || indexing_.load()) {
            stop();
            if (commit) {
                commit({}, {}, GlobalFileCommitMode::ResetAll);
            }
            signature_.clear();
            {
                std::lock_guard lock(stateMutex_);
                rescanPausedUntil_ = {};
            }
        }
        return;
    }

    if (indexing_.load()) {
        return;
    }
    if (signature_ == desiredSignature) {
        return;
    }

    // After cancel, keep the current signature so per-frame sync() does not immediately restart
    // the worker. A changed signature is still held back during the cooldown.
    bool rescanPaused = false;
    {
        std::lock_guard lock(stateMutex_);
        rescanPaused = rescanPausedUntil_.time_since_epoch().count() != 0 && std::chrono::steady_clock::now() < rescanPausedUntil_;
    }
    if (rescanPaused) {
        if (signature_.empty()) {
            signature_ = desiredSignature;
        }
        return;
    }

    signature_ = desiredSignature;
    start(std::move(cachePath), std::move(commit), false, clampScanIntensity(scanIntensity));
}

void GlobalFileIndex::rebuild(std::filesystem::path cachePath, int scanIntensity, CommitCallback commit)
{
    if (cachePath.empty()) {
        return;
    }
    scanIntensity = clampScanIntensity(scanIntensity);
    scanIntensity_.store(scanIntensity);
    LocalVolumeFileProvider provider;
    signature_ = provider.signature() + "|" + cachePath.string() + "|i" + std::to_string(scanIntensity);
    {
        std::lock_guard lock(stateMutex_);
        rescanPausedUntil_ = {};
    }
    start(std::move(cachePath), std::move(commit), true, scanIntensity);
}

void GlobalFileIndex::start(std::filesystem::path cachePath, CommitCallback commit, bool forceRebuild, int scanIntensity)
{
    stop();
    stopRequested_.store(false);
    aggressiveRebuild_.store(forceRebuild);
    scanIntensity = clampScanIntensity(scanIntensity);
    scanIntensity_.store(scanIntensity);
    indexing_.store(true);
    worker_ = std::thread([this, cachePath = std::move(cachePath), commit = std::move(commit), forceRebuild, scanIntensity]() mutable {
        const ScanIntensityProfile profile = intensityProfile(scanIntensity);
        LocalVolumeFileProvider provider;
        const std::string providerSignature = provider.signature();
        // A manual rebuild replaces roots one by one. Keep the current snapshot until
        // each replacement arrives so cancelling never blanks global search results.
        if (commit && !forceRebuild) {
            commit({}, {}, GlobalFileCommitMode::ResetAll);
        }
        // Never wipe all root caches up front. Each finished root replaces its own cache
        // atomically, so cancelling a rebuild still leaves previous roots searchable.

        std::vector<GlobalScanRoot> roots = provider.enumerateRoots();
        std::vector<RootRuntimeState> states;
        states.reserve(roots.size());
        for (GlobalScanRoot& root : roots) {
            RootRuntimeState state;
            state.root = std::move(root);
            state.nextScan = std::chrono::steady_clock::now();
            states.push_back(std::move(state));
        }
        const auto totalRoots = static_cast<std::uint32_t>(states.size());
        std::uint32_t completedRoots = 0;
        std::uint64_t indexedFiles = 0;
        setProgress(true, forceRebuild, 0, totalRoots, 0, 0, 0.0f, {}, {}, forceRebuild ? "rebuilding" : "loading");

        // Prefer root-partitioned caches; fall back to legacy monolithic cache once.
        bool anyRootCache = false;
        bool legacyActive = false;
        // Finish loading the on-disk baseline even when a cancellation is requested.
        // It happens in the worker, so UI cancellation remains responsive while old
        // root/legacy caches continue to be made searchable.
        std::atomic_bool ignoreStop{false};
        for (RootRuntimeState& state : states) {
            std::vector<GlobalFileRecord> cached;
            if (loadRootCache(cachePath, state.root.id, ignoreStop, cached)) {
                anyRootCache = true;
                state.loadedFromCache = true;
                state.scannedOnce = true;
                indexedFiles += cached.size();
                ++completedRoots;
                setProgress(true, forceRebuild, completedRoots, totalRoots, indexedFiles, 0, 1.0f, state.root.id, {}, "loading");
                // Cached roots: delay live refresh. Manual rebuild still rescans soon, but only after baseline is loaded.
                if (forceRebuild) {
                    state.nextScan = std::chrono::steady_clock::now() + std::chrono::seconds(state.root.hot ? 0 : 1);
                } else {
                    state.nextScan = std::chrono::steady_clock::now() + (state.root.hot ? profile.cacheHotDelay : profile.cacheColdDelay);
                }
                commitRootReplace(commit, state.root.id, std::move(cached));
            }
        }

        // If a cancel cooldown is active and this is not a manual rebuild, stop after loading caches.
        std::chrono::steady_clock::time_point pauseUntil;
        {
            std::lock_guard lock(stateMutex_);
            pauseUntil = rescanPausedUntil_;
        }
        const bool pauseAutomaticRescan =
            !forceRebuild && pauseUntil.time_since_epoch().count() != 0 && std::chrono::steady_clock::now() < pauseUntil;
        if (pauseAutomaticRescan) {
            indexing_.store(false);
            clearProgress();
            return;
        }

        if (!anyRootCache) {
            // Keep old cache searchable immediately under a temporary root, then rebuild partitioned caches.
            legacyActive = loadLegacyCache(cachePath, ignoreStop, commit);
            for (RootRuntimeState& state : states) {
                // Hot first so newly created user files become searchable quickly.
                // Manual rebuild starts all roots ASAP with less throttling.
                state.nextScan =
                    std::chrono::steady_clock::now() +
                    (forceRebuild ? std::chrono::seconds(0) : (state.root.hot ? std::chrono::seconds(0) : profile.missingColdDelay));
            }
        } else if (forceRebuild) {
            // Baseline caches are already searchable; now rescan roots one by one and replace them.
            for (RootRuntimeState& state : states) {
                state.nextScan = std::chrono::steady_clock::now() + std::chrono::seconds(state.root.hot ? 0 : 1);
            }
        }

        writeCacheSignature(cachePath, providerSignature);

        if (stopRequested_.load()) {
            indexing_.store(false);
            clearProgress();
            return;
        }

        auto allRootsReady = [&]() {
            if (states.empty()) {
                return true;
            }
            for (const RootRuntimeState& state : states) {
                if (!state.scannedOnce && !state.loadedFromCache) {
                    return false;
                }
            }
            return true;
        };

        auto clearLegacyIfNeeded = [&]() {
            if (!legacyActive || !commit) {
                return;
            }
            // Keep the legacy snapshot until every partitioned root has been loaded or scanned once.
            if (!allRootsReady()) {
                return;
            }
            commit("legacy", {}, GlobalFileCommitMode::ReplaceRoot);
            legacyActive = false;
        };

        if (anyRootCache || legacyActive) {
            // Cached data is already searchable; periodic rescan should stay quiet.
            indexing_.store(false);
        }

        size_t coldCursor = 0;
        bool aggressivePass = forceRebuild;
        while (!stopRequested_.load()) {
            bool didWork = false;
            const auto now = std::chrono::steady_clock::now();
            const std::vector<std::filesystem::path> skipForCold = hotRootPaths(states);
            const bool bootstrapPass = !allRootsReady();

            const GlobalScanPriority scanPriority = aggressivePass ? GlobalScanPriority::Normal : profile.backgroundPriority;
            const auto rootPause = aggressivePass ? kAggressiveBetweenRootPause : profile.betweenRootPause;

            // Hot roots: short-period full replace.
            for (RootRuntimeState& state : states) {
                if (stopRequested_.load()) {
                    break;
                }
                if (!state.root.hot || now < state.nextScan) {
                    continue;
                }
                if (bootstrapPass || aggressivePass) {
                    indexing_.store(true);
                }
                const bool wasReady = state.scannedOnce || state.loadedFromCache;
                const std::uint64_t indexedBefore = indexedFiles;
                setProgress(true, aggressivePass, completedRoots, totalRoots, indexedFiles, 0, 0.0f, state.root.id, {},
                            aggressivePass ? "rebuilding" : "scanning");
                if (refreshRoot(provider, state, {}, cachePath, stopRequested_, commit, scanPriority, profile.hotRefresh,
                                profile.coldRefresh, &indexedFiles, [&](std::uint64_t scannedInRoot, std::string currentPath) {
                                    setProgress(true, aggressivePass, completedRoots, totalRoots, indexedBefore + scannedInRoot,
                                                scannedInRoot, estimateRootFraction(scannedInRoot), state.root.id, std::move(currentPath),
                                                aggressivePass ? "rebuilding" : "scanning");
                                })) {
                    if (!wasReady) {
                        ++completedRoots;
                    }
                    setProgress(true, aggressivePass, completedRoots, totalRoots, indexedFiles, 0, 1.0f, state.root.id, {},
                                aggressivePass ? "rebuilding" : "scanning");
                    clearLegacyIfNeeded();
                }
                didWork = true;
                if (waitForStopOrTimeout(stopRequested_, rootPause)) {
                    break;
                }
            }
            if (stopRequested_.load()) {
                break;
            }

            // Cold roots: roll one overdue root at a time to keep occupancy low.
            // Manual rebuild still rolls one root at a time, but with shorter pauses and normal IO priority.
            if (!states.empty()) {
                for (size_t attempt = 0; attempt < states.size(); ++attempt) {
                    if (stopRequested_.load()) {
                        break;
                    }
                    RootRuntimeState& state = states[coldCursor % states.size()];
                    coldCursor = (coldCursor + 1) % states.size();
                    if (state.root.hot || now < state.nextScan) {
                        continue;
                    }
                    if (bootstrapPass || aggressivePass) {
                        indexing_.store(true);
                    }
                    const bool wasReady = state.scannedOnce || state.loadedFromCache;
                    const std::uint64_t indexedBefore = indexedFiles;
                    setProgress(true, aggressivePass, completedRoots, totalRoots, indexedFiles, 0, 0.0f, state.root.id, {},
                                aggressivePass ? "rebuilding" : "scanning");
                    if (refreshRoot(provider, state, skipForCold, cachePath, stopRequested_, commit, scanPriority, profile.hotRefresh,
                                    profile.coldRefresh, &indexedFiles, [&](std::uint64_t scannedInRoot, std::string currentPath) {
                                        setProgress(true, aggressivePass, completedRoots, totalRoots, indexedBefore + scannedInRoot,
                                                    scannedInRoot, estimateRootFraction(scannedInRoot), state.root.id,
                                                    std::move(currentPath), aggressivePass ? "rebuilding" : "scanning");
                                    })) {
                        if (!wasReady) {
                            ++completedRoots;
                        }
                        setProgress(true, aggressivePass, completedRoots, totalRoots, indexedFiles, 0, 1.0f, state.root.id, {},
                                    aggressivePass ? "rebuilding" : "scanning");
                        clearLegacyIfNeeded();
                    }
                    didWork = true;
                    break;
                }
            }

            if (allRootsReady()) {
                indexing_.store(false);
                if (aggressivePass) {
                    aggressivePass = false;
                    aggressiveRebuild_.store(false);
                    {
                        std::lock_guard lock(stateMutex_);
                        rescanPausedUntil_ = {};
                    }
                    setProgress(false, false, completedRoots, totalRoots, indexedFiles, 0, 1.0f, {}, {}, "done");
                } else if (!bootstrapPass) {
                    setProgress(false, false, completedRoots, totalRoots, indexedFiles, 0, 1.0f, {}, {}, "idle");
                }
            }

            if (!didWork) {
                if (waitForStopOrTimeout(stopRequested_, aggressivePass ? rootPause : profile.idlePoll)) {
                    break;
                }
            } else if (waitForStopOrTimeout(stopRequested_, rootPause)) {
                break;
            }
        }

        aggressiveRebuild_.store(false);
        indexing_.store(false);
        clearProgress();
    });
}

void GlobalFileIndex::cancel()
{
    // Keep signature_ so the per-frame sync() does not treat this as "needs start"
    // and immediately relaunch a full index after a manual cancel.
    // Also pause automatic rescans so cancel keeps serving existing root caches for a while.
    {
        std::lock_guard lock(stateMutex_);
        rescanPausedUntil_ = std::chrono::steady_clock::now() + kCancelRescanCooldown;
    }
    stopRequested_.store(true);
}

void GlobalFileIndex::stop()
{
    stopRequested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    indexing_.store(false);
    aggressiveRebuild_.store(false);
    clearProgress();
    stopRequested_.store(false);
}

} // namespace launcher

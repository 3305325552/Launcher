#pragma once

#include "core/GlobalFileItem.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace launcher {

enum class GlobalFileCommitMode {
    ResetAll,
    ReplaceRoot,
    AppendRoot,
};

struct GlobalIndexProgress {
    bool active = false;
    bool rebuilding = false;
    std::uint32_t completedRoots = 0;
    std::uint32_t totalRoots = 0;
    std::uint64_t indexedFiles = 0;
    std::uint64_t currentRootFiles = 0;
    float currentRootFraction = 0.0f;
    std::string currentRoot;
    std::string currentPath;
    std::string phase;
};

class GlobalFileIndex {
public:
    using CommitCallback = std::function<void(const std::string& rootId, std::vector<GlobalFileRecord> files, GlobalFileCommitMode mode)>;

    ~GlobalFileIndex();

    // scanIntensity: 0=Low, 1=Balanced, 2=High (High stays modest; never full-speed foreground).
    void sync(bool enabled, std::filesystem::path cachePath, int scanIntensity, CommitCallback commit);
    void rebuild(std::filesystem::path cachePath, int scanIntensity, CommitCallback commit);
    void cancel();
    void stop();
    bool indexing() const
    {
        return indexing_.load();
    }
    GlobalIndexProgress progress() const;

private:
    void start(std::filesystem::path cachePath, CommitCallback commit, bool forceRebuild, int scanIntensity);
    void setProgress(bool active, bool rebuilding, std::uint32_t completedRoots, std::uint32_t totalRoots, std::uint64_t indexedFiles,
                     std::uint64_t currentRootFiles, float currentRootFraction, std::string currentRoot, std::string currentPath,
                     std::string phase);
    void clearProgress();

    std::thread worker_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool indexing_{false};
    std::atomic_bool aggressiveRebuild_{false};
    std::atomic_int scanIntensity_{1};
    std::string signature_;
    // After a manual cancel, keep serving existing root caches and delay automatic rescans.
    mutable std::mutex stateMutex_;
    std::chrono::steady_clock::time_point rescanPausedUntil_{};

    mutable std::mutex progressMutex_;
    GlobalIndexProgress progress_{};
};

} // namespace launcher

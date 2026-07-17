#pragma once

#include "core/GlobalFileProvider.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace launcher {

struct GlobalScanRoot {
    std::string id;
    std::filesystem::path path;
    bool hot = false;
};

enum class GlobalScanPriority {
    Background,
    Normal,
};

class LocalVolumeFileProvider final : public GlobalFileProvider {
public:
    using ProgressCallback = std::function<void(std::uint64_t scannedInRoot, std::string currentPath)>;

    std::string signature() const override;
    std::vector<GlobalFileRecord> scan(const std::atomic_bool& stop, bool incrementalCommit, bool collectAll,
                                       BatchCallback commitBatch) override;

    std::vector<GlobalScanRoot> enumerateRoots() const;
    std::vector<GlobalFileRecord> scanRoot(const GlobalScanRoot& root, const std::vector<std::filesystem::path>& skipRoots,
                                           const std::atomic_bool& stop, bool incrementalCommit, bool collectAll, BatchCallback commitBatch,
                                           GlobalScanPriority priority = GlobalScanPriority::Background, ProgressCallback onProgress = {});
};

} // namespace launcher

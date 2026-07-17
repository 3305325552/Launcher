#pragma once

#include "core/GlobalFileItem.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace launcher {

inline constexpr size_t kGlobalFileCommitBatchSize = 8192;

class GlobalFileProvider {
public:
    using BatchCallback = std::function<void(std::vector<GlobalFileRecord>)>;

    virtual ~GlobalFileProvider() = default;

    virtual std::string signature() const = 0;
    virtual std::vector<GlobalFileRecord> scan(const std::atomic_bool& stop, bool incrementalCommit, bool collectAll,
                                               BatchCallback commitBatch) = 0;
};

} // namespace launcher

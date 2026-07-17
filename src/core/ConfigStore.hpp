#pragma once

#include "launcher/Models.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace launcher {

class ConfigStore {
public:
    ConfigStore();
    explicit ConfigStore(std::filesystem::path configPath);

    std::optional<PersistedState> tryLoadPersisted(std::string* error = nullptr) const;
    PersistedState loadPersisted() const;
    void save(const PersistedState& state) const;

    AppState load() const;
    void save(const AppState& state) const;
    std::filesystem::path directory() const
    {
        return configPath_.parent_path();
    }
    const std::filesystem::path& path() const
    {
        return configPath_;
    }
    bool moveConfigDirectory(const std::filesystem::path& directory, std::string* error = nullptr);

private:
    std::filesystem::path defaultConfigPath_;
    std::filesystem::path locationPath_;
    std::filesystem::path configPath_;
};

} // namespace launcher

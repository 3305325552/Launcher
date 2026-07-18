#pragma once

#include "launcher/ThemeTypes.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace launcher {

class ThemeStore {
public:
    void reload(const std::filesystem::path& builtinDirectory, const std::filesystem::path& customDirectory, const std::string& selectedId);

    const std::vector<ThemeCatalogEntry>& entries() const
    {
        return entries_;
    }

    const ThemeDefinition& active() const
    {
        return active_;
    }

    bool select(const std::string& id);
    bool getTheme(const std::string& id, ThemeDefinition& theme) const;
    void setPreview(ThemeDefinition theme);
    bool saveCustomTheme(const ThemeDefinition& theme, std::string* selectedId = nullptr, std::string* error = nullptr);

    const std::filesystem::path& builtinDirectory() const
    {
        return builtinDirectory_;
    }

    const std::filesystem::path& customDirectory() const
    {
        return customDirectory_;
    }

private:
    std::filesystem::path builtinDirectory_;
    std::filesystem::path customDirectory_;
    std::vector<ThemeDefinition> themes_;
    std::vector<ThemeCatalogEntry> entries_;
    ThemeDefinition active_;
};

ThemeDefinition fallbackTheme(bool dark);
bool loadThemeFile(const std::filesystem::path& path, bool builtin, ThemeDefinition& theme, std::string* error = nullptr);
bool saveThemeFile(const ThemeDefinition& theme, const std::filesystem::path& path, std::string* error = nullptr);

} // namespace launcher

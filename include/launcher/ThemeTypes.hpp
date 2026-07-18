#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace launcher {

struct ThemeColor {
    std::string key;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct ThemeBackground {
    bool enabled = false;
    std::filesystem::path imagePath;
    std::string imageEmbeddedName;
    std::string imageEmbeddedMime;
    std::string imageEmbeddedBase64;
    int imageMode = 0;
    int opacity = 100;
    bool animated = false;
    int animationFps = 15;
    int animationMaxWidth = 960;
    int animationQuality = 8;
};

struct ThemeDefinition {
    std::string id = "builtin:dark";
    std::string name = "Dark";
    std::string author = "Launcher";
    bool dark = true;
    bool windowTransparent = false;
    int windowOpacity = 100;
    bool windowTransparencyForAll = false;
    int secondaryWindowOpacity = 100;
    int popupMenuOpacity = 100;
    ThemeBackground background;
    std::vector<ThemeColor> colors;
    std::filesystem::path sourcePath;
    bool builtin = true;
};

struct ThemeCatalogEntry {
    std::string id;
    std::string name;
    std::string author;
    std::filesystem::path path;
    bool builtin = false;
};

} // namespace launcher

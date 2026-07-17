#pragma once

#include "core/StringEncoding.hpp"

#include <dwmapi.h>
#include <filesystem>
#include <string>
#include <windows.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace launcher {

inline std::filesystem::path getAssetDir()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path() / "assets";
}

class SystemIntegration {
public:
    static void registerTaskbarAppId();
    static bool setStartupEnabled(bool enabled);
    static bool isStartupEnabled();
    static bool setDirectorySearchContextMenuEnabled(bool enabled);
    static bool isDirectorySearchContextMenuEnabled();
    static void refreshDirectorySearchContextMenu();
};

} // namespace launcher

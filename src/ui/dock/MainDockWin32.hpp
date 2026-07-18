#pragma once

#include "launcher/Models.hpp"

#include <windows.h>

#include <filesystem>
#include <string>

namespace launcher {

std::filesystem::path appFolder();
HWND mainWindowHandle();
HWND dialogOwnerHandle();
void bringWindowToDialogFront(HWND hwnd);

std::string openFileDialog(const wchar_t* title, const wchar_t* filter);
std::string saveFileDialog(const wchar_t* title, const wchar_t* filter);
std::string selectFolderDialog(const wchar_t* title, const std::filesystem::path& initial = {});
std::string pickIconDialog(const std::string& current);

void openContainingFolder(const LaunchItem& item);
void openWithDialog(const LaunchItem& item);
void showFileProperties(const LaunchItem& item);
void openAppFolder();
void broadcastEnvironmentChanged();
void openUrl(const wchar_t* url);
void openSystemTool(const wchar_t* command, const wchar_t* args = nullptr);
void centerMainWindow();

} // namespace launcher

#pragma once

namespace launcher::app_identity {

inline constexpr char kDisplayName[] = "Launcher";
inline constexpr char kVersion[] = LAUNCHER_VERSION;
inline constexpr wchar_t kSingleInstanceMutexName[] = L"Launcher.SingleInstance";
inline constexpr wchar_t kWindowClassName[] = L"LauncherWindow";
inline constexpr wchar_t kTaskbarAppId[] = L"Launcher.Desktop";
inline constexpr unsigned long long kSearchTextCopyDataId = 0x4D415945;
inline constexpr wchar_t kConfigDirectoryName[] = L"Launcher";
inline constexpr wchar_t kLegacyConfigDirectoryName[] = L"LauncherReplica";
inline constexpr wchar_t kLatestUpdateDownloadUrl[] = L"https://github.com/3305325552/Launcher/releases/latest/download/";
inline constexpr wchar_t kReleasesUrl[] = L"https://github.com/3305325552/Launcher/releases";
inline constexpr wchar_t kDocsUrl[] = L"https://github.com/3305325552/Launcher";

} // namespace launcher::app_identity

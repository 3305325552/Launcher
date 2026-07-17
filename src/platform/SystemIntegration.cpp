#include "platform/SystemIntegration.hpp"

#include "launcher/AppIdentity.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <string>

namespace launcher {
namespace {

constexpr const wchar_t* kAppId = app_identity::kTaskbarAppId;
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Launcher";
constexpr wchar_t kLegacyRunValue[] = L"Launcher Replica";
constexpr wchar_t kDirectoryShellKey[] = L"Software\\Classes\\Directory\\shell\\LauncherSearchHere";
constexpr wchar_t kDirectoryBackgroundShellKey[] = L"Software\\Classes\\Directory\\Background\\shell\\LauncherSearchHere";
constexpr wchar_t kContextMenuText[] = L"Search here with Launcher";

std::wstring exePath()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

std::wstring directorySearchCommand(const wchar_t* placeholder)
{
    return L"\"" + exePath() + L"\" --search-dir \"" + placeholder + L"\"";
}

bool setStringValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName, const std::wstring& value)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const bool ok = RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

bool deleteTreeIfExists(HKEY root, const wchar_t* subKey)
{
    const LSTATUS status = RegDeleteTreeW(root, subKey);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

bool keyExists(HKEY root, const wchar_t* subKey)
{
    HKEY key = nullptr;
    const bool exists = RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS;
    if (key) {
        RegCloseKey(key);
    }
    return exists;
}

bool writeDirectorySearchMenuKey(const wchar_t* shellKey, const wchar_t* placeholder)
{
    const std::wstring commandKey = std::wstring(shellKey) + L"\\command";
    bool ok = setStringValue(HKEY_CURRENT_USER, shellKey, nullptr, kContextMenuText);
    ok &= setStringValue(HKEY_CURRENT_USER, shellKey, L"Icon", exePath());
    ok &= setStringValue(HKEY_CURRENT_USER, commandKey.c_str(), nullptr, directorySearchCommand(placeholder));
    return ok;
}

} // namespace

void SystemIntegration::registerTaskbarAppId()
{
    SetCurrentProcessExplicitAppUserModelID(kAppId);
}

bool SystemIntegration::setStartupEnabled(bool enabled)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const auto closeKey = [&]() {
        RegCloseKey(key);
    };
    if (!enabled) {
        const LSTATUS status = RegDeleteValueW(key, kRunValue);
        const LSTATUS legacyStatus = RegDeleteValueW(key, kLegacyRunValue);
        const bool ok = (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND) &&
                        (legacyStatus == ERROR_SUCCESS || legacyStatus == ERROR_FILE_NOT_FOUND);
        closeKey();
        return ok;
    }

    RegDeleteValueW(key, kLegacyRunValue);
    std::wstring command = L"\"" + exePath() + L"\"";
    const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const bool ok = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), bytes) == ERROR_SUCCESS;
    closeKey();
    return ok;
}

bool SystemIntegration::isStartupEnabled()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[MAX_PATH * 2]{};
    DWORD type = REG_SZ;
    DWORD bytes = sizeof(value);
    bool ok = RegQueryValueExW(key, kRunValue, nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes) == ERROR_SUCCESS && type == REG_SZ;
    if (!ok) {
        bytes = sizeof(value);
        type = REG_SZ;
        ok = RegQueryValueExW(key, kLegacyRunValue, nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes) == ERROR_SUCCESS &&
             type == REG_SZ;
    }
    RegCloseKey(key);
    return ok;
}

bool SystemIntegration::setDirectorySearchContextMenuEnabled(bool enabled)
{
    bool ok = true;
    if (!enabled) {
        ok = deleteTreeIfExists(HKEY_CURRENT_USER, kDirectoryShellKey);
        ok &= deleteTreeIfExists(HKEY_CURRENT_USER, kDirectoryBackgroundShellKey);
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        return ok;
    }

    ok = writeDirectorySearchMenuKey(kDirectoryShellKey, L"%1");
    ok &= writeDirectorySearchMenuKey(kDirectoryBackgroundShellKey, L"%V");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool SystemIntegration::isDirectorySearchContextMenuEnabled()
{
    return keyExists(HKEY_CURRENT_USER, kDirectoryShellKey) && keyExists(HKEY_CURRENT_USER, kDirectoryBackgroundShellKey);
}

void SystemIntegration::refreshDirectorySearchContextMenu()
{
    if (isDirectorySearchContextMenuEnabled()) {
        setDirectorySearchContextMenuEnabled(true);
    }
}

} // namespace launcher

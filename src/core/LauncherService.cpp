#include "core/LauncherService.hpp"

#include "core/StringEncoding.hpp"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>

namespace launcher {
namespace {

std::wstring lower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

std::wstring quoteArgument(const std::wstring& value)
{
    std::wstring result = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') {
            result += L"\\\"";
        } else {
            result += ch;
        }
    }
    result += L"\"";
    return result;
}

bool canCreateProcessDirectly(const std::filesystem::path& target)
{
    const std::wstring ext = lower(target.extension().wstring());
    return ext == L".exe" || ext == L".com";
}

bool createProcessLaunch(const LaunchItem& item, int showCommand)
{
    if (item.runAsAdmin || !canCreateProcessDirectly(item.target)) {
        return false;
    }

    const std::wstring target = item.target.wstring();
    std::wstring commandLine = quoteArgument(target);
    const std::wstring args = widen(item.arguments);
    if (!args.empty()) {
        commandLine += L" ";
        commandLine += args;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = static_cast<WORD>(showCommand);

    PROCESS_INFORMATION process{};
    const std::wstring startDir = item.startDirectory.wstring();
    const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                        startDir.empty() ? nullptr : startDir.c_str(), &startup, &process);
    if (!created) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

} // namespace

bool LauncherService::launch(const LaunchItem& item, int showCommand) const
{
    if (item.type == LaunchItemType::Placeholder || item.type == LaunchItemType::Note || item.target.empty()) {
        return false;
    }

    if (showCommand == SW_SHOWMINIMIZED && createProcessLaunch(item, showCommand)) {
        return true;
    }

    const std::wstring verb = item.runAsAdmin ? L"runas" : L"open";
    const std::wstring target = item.target.wstring();
    const std::wstring args = widen(item.arguments);
    const std::wstring startDir = item.startDirectory.wstring();

    HINSTANCE result = ShellExecuteW(nullptr, verb.c_str(), target.c_str(), args.empty() ? nullptr : args.c_str(),
                                     startDir.empty() ? nullptr : startDir.c_str(), showCommand);

    return reinterpret_cast<intptr_t>(result) > 32;
}

} // namespace launcher

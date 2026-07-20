#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

namespace launcher {

inline std::wstring widen(const std::string& value)
{
    if (value.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

inline std::string narrow(const std::wstring& value)
{
    if (value.empty()) {
        return {};
    }
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

inline std::string pathToUtf8(const std::filesystem::path& value)
{
    return narrow(value.wstring());
}

inline std::filesystem::path pathFromUtf8(const std::string& value)
{
    return std::filesystem::path(widen(value));
}

} // namespace launcher

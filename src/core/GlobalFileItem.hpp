#pragma once

#include "core/StringEncoding.hpp"
#include "launcher/Models.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace launcher {

struct GlobalFileRecord {
    std::string path;
    std::uint32_t nameOffset = 0;
    std::uint32_t parentLength = 0;
    bool directory = false;
    std::uint32_t modifiedTime = 0;
};

struct IndexedGlobalFileRecord {
    const char* nameData = nullptr;
    const char* parentPathData = nullptr;
    std::uint32_t nameLength = 0;
    std::uint32_t parentPathLength = 0;
    bool directory = false;
    std::uint32_t modifiedTime = 0;
};

inline size_t globalFileNameOffset(std::string_view path)
{
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string_view::npos || slash + 1 >= path.size()) {
        return 0;
    }
    return slash + 1;
}

inline size_t globalFileParentLength(std::string_view path, size_t nameOffset)
{
    if (nameOffset == 0) {
        return 0;
    }
    const size_t separator = nameOffset - 1;
    if (separator == 0) {
        return 1;
    }
    if (separator == 2 && path.size() >= 3 && path[1] == ':') {
        return 3;
    }
    return separator;
}

inline std::string_view globalFileName(const GlobalFileRecord& record)
{
    if (record.nameOffset < record.path.size()) {
        return std::string_view(record.path).substr(record.nameOffset);
    }
    return record.path;
}

inline std::string_view globalFileName(const IndexedGlobalFileRecord& record)
{
    return record.nameData ? std::string_view(record.nameData, record.nameLength) : std::string_view{};
}

inline std::string_view globalFileParentPath(const GlobalFileRecord& record)
{
    if (record.parentLength > 0 && record.parentLength <= record.path.size()) {
        return std::string_view(record.path).substr(0, record.parentLength);
    }
    return {};
}

inline std::string_view globalFileParentPath(const IndexedGlobalFileRecord& record)
{
    return record.parentPathData ? std::string_view(record.parentPathData, record.parentPathLength) : std::string_view{};
}

inline std::string globalFilePath(const GlobalFileRecord& record)
{
    return record.path;
}

inline std::string globalFilePath(const IndexedGlobalFileRecord& record)
{
    const std::string_view name = globalFileName(record);
    const std::string_view parentPath = globalFileParentPath(record);
    if (parentPath.empty()) {
        return std::string(name);
    }
    std::string path;
    path.reserve(parentPath.size() + name.size() + 1);
    path.append(parentPath);
    const char tail = parentPath.back();
    if (tail != '/' && tail != '\\') {
        path.push_back('/');
    }
    path.append(name);
    return path;
}

inline GlobalFileRecord makeGlobalFileRecord(const std::filesystem::path& path, bool directory, std::uint32_t modifiedTime = 0)
{
    const std::string pathText = narrow(path.wstring());
    GlobalFileRecord record;
    record.path = pathText;
    const size_t nameOffset = globalFileNameOffset(record.path);
    record.nameOffset = static_cast<std::uint32_t>(std::min<size_t>(nameOffset, std::numeric_limits<std::uint32_t>::max()));
    const size_t parentLength = globalFileParentLength(record.path, nameOffset);
    record.parentLength = static_cast<std::uint32_t>(std::min<size_t>(parentLength, std::numeric_limits<std::uint32_t>::max()));
    record.directory = directory;
    record.modifiedTime = modifiedTime;
    return record;
}

inline GlobalFileRecord makeGlobalFileRecord(std::string pathText, bool directory, std::uint32_t modifiedTime = 0)
{
    GlobalFileRecord record;
    record.path = std::move(pathText);
    const size_t nameOffset = globalFileNameOffset(record.path);
    record.nameOffset = static_cast<std::uint32_t>(std::min<size_t>(nameOffset, std::numeric_limits<std::uint32_t>::max()));
    const size_t parentLength = globalFileParentLength(record.path, nameOffset);
    record.parentLength = static_cast<std::uint32_t>(std::min<size_t>(parentLength, std::numeric_limits<std::uint32_t>::max()));
    record.directory = directory;
    record.modifiedTime = modifiedTime;
    return record;
}

inline std::filesystem::path pathFromUtf8(const std::string& utf8Path)
{
    std::wstring wide = widen(utf8Path);
    for (wchar_t& ch : wide) {
        if (ch == L'/') {
            ch = L'\\';
        }
    }
    return std::filesystem::path(wide);
}

inline LaunchItem makeGlobalFileItem(const GlobalFileRecord& record)
{
    LaunchItem item;
    item.id = "global-file:" + record.path;
    item.name = std::string(globalFileName(record));
    item.subtitle = std::string(globalFileParentPath(record));
    item.target = pathFromUtf8(record.path);
    item.startDirectory = record.directory ? item.target : pathFromUtf8(item.subtitle);
    item.type = LaunchItemType::App;
    item.fallbackColor = record.directory ? "#5B8DEFff" : "#8C8C8CFF";
    return item;
}

inline LaunchItem makeGlobalFileItem(const IndexedGlobalFileRecord& record)
{
    const std::string pathText = globalFilePath(record);
    LaunchItem item;
    item.id = "global-file:" + pathText;
    item.name = std::string(globalFileName(record));
    item.subtitle = std::string(globalFileParentPath(record));
    item.target = pathFromUtf8(pathText);
    item.startDirectory = record.directory ? item.target : pathFromUtf8(item.subtitle);
    item.type = LaunchItemType::App;
    item.fallbackColor = record.directory ? "#5B8DEFff" : "#8C8C8CFF";
    return item;
}

} // namespace launcher

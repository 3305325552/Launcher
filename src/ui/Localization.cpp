#include "ui/Localization.hpp"

#include "platform/SystemIntegration.hpp"
#include <windows.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>

namespace launcher {
namespace {

std::unordered_map<std::string, std::string> gStrings;
std::string gLoadedLanguage;
std::filesystem::file_time_type gLoadedLocaleWriteTime{};

std::filesystem::path localePath(const std::string& language)
{
    return getAssetDir() / "locales" / (language + ".json");
}

std::string normalizedLanguage(std::string language)
{
    if (language.empty() || language == "auto") {
        return "zh-CN";
    }
    return language;
}

std::string loadFileText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void flattenJson(const nlohmann::json& node, const std::string& prefix)
{
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            flattenJson(it.value(), prefix.empty() ? it.key() : prefix + "." + it.key());
        }
        return;
    }
    if (node.is_string()) {
        gStrings[prefix] = node.get<std::string>();
    }
}

std::wstring utf8ToWide(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::string wideToUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::string nativeLocaleName(const std::string& id)
{
    const std::wstring locale = utf8ToWide(id);
    if (locale.empty()) {
        return {};
    }
    wchar_t buffer[LOCALE_NAME_MAX_LENGTH]{};
    const int count = GetLocaleInfoEx(locale.c_str(), LOCALE_SNATIVEDISPLAYNAME, buffer, LOCALE_NAME_MAX_LENGTH);
    if (count <= 1) {
        return {};
    }
    return wideToUtf8(buffer);
}

std::string localeMetadataName(const std::filesystem::path& path)
{
    try {
        const nlohmann::json root = nlohmann::json::parse(loadFileText(path));
        if (!root.is_object()) {
            return {};
        }
        if (const auto it = root.find("__locale"); it != root.end() && it->is_object()) {
            return it->value("name", it->value("nativeName", std::string{}));
        }
    } catch (...) {}
    return {};
}

} // namespace

void setLocale(std::string language)
{
    language = normalizedLanguage(std::move(language));
    std::filesystem::path path = localePath(language);
    std::string text = loadFileText(path);
    if (text.empty() && language != "zh-CN") {
        path = localePath("zh-CN");
        text = loadFileText(path);
    }
    if (text.empty()) {
        return;
    }

    std::error_code ec;
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    if (language == gLoadedLanguage && !ec && writeTime == gLoadedLocaleWriteTime) {
        return;
    }

    gStrings.clear();
    gLoadedLanguage = language;
    gLoadedLocaleWriteTime = ec ? std::filesystem::file_time_type{} : writeTime;

    try {
        flattenJson(nlohmann::json::parse(text), {});
    } catch (...) {
        gStrings.clear();
    }
}

std::vector<LocaleCatalogEntry> availableLocales()
{
    std::vector<LocaleCatalogEntry> entries;
    const std::filesystem::path directory = getAssetDir() / "locales";
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec)) {
        return entries;
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec || !entry.is_regular_file(ec) || entry.path().extension() != ".json") {
            continue;
        }
        LocaleCatalogEntry locale;
        locale.id = entry.path().stem().string();
        locale.path = entry.path();
        locale.name = localeMetadataName(locale.path);
        if (locale.name.empty()) {
            locale.name = nativeLocaleName(locale.id);
        }
        if (locale.name.empty()) {
            locale.name = locale.id;
        }
        entries.push_back(std::move(locale));
    }
    std::sort(entries.begin(), entries.end(), [](const LocaleCatalogEntry& lhs, const LocaleCatalogEntry& rhs) {
        return lhs.id < rhs.id;
    });
    return entries;
}

const char* tr(const char* key)
{
    if (gLoadedLanguage.empty()) {
        setLocale("zh-CN");
    }
    const auto it = gStrings.find(key);
    if (it == gStrings.end()) {
        return key;
    }
    return it->second.c_str();
}

std::string trs(const char* key)
{
    return tr(key);
}

std::wstring trw(const char* key)
{
    return utf8ToWide(trs(key));
}

std::string trId(const char* key, const char* id)
{
    std::string result = trs(key);
    result += id;
    return result;
}

std::string comboText(std::initializer_list<const char*> keys)
{
    std::string result;
    for (const char* key : keys) {
        result += tr(key);
        result.push_back('\0');
    }
    result.push_back('\0');
    return result;
}

std::wstring fileDialogFilter(std::initializer_list<std::pair<const char*, const wchar_t*>> entries)
{
    std::wstring result;
    for (const auto& entry : entries) {
        result += trw(entry.first);
        result.push_back(L'\0');
        result += entry.second;
        result.push_back(L'\0');
    }
    result.push_back(L'\0');
    return result;
}

} // namespace launcher

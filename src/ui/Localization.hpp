#pragma once

#include <initializer_list>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace launcher {

struct LocaleCatalogEntry {
    std::string id;
    std::string name;
    std::filesystem::path path;
};

void setLocale(std::string language);
std::vector<LocaleCatalogEntry> availableLocales();
const char* tr(const char* key);
std::string trs(const char* key);
std::wstring trw(const char* key);
std::string trId(const char* key, const char* id);
std::string comboText(std::initializer_list<const char*> keys);
std::wstring fileDialogFilter(std::initializer_list<std::pair<const char*, const wchar_t*>> entries);

} // namespace launcher

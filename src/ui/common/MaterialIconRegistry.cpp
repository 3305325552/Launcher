#include "ui/common/MaterialIconRegistry.hpp"

#include "platform/SystemIntegration.hpp"
#include "ui/common/MaterialIcons.hpp"

#include <fstream>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace launcher {
namespace {

std::vector<MaterialIconInfo> gIcons;
std::unordered_map<std::string, std::size_t> gIconIndex;
ImVector<ImWchar> gIconRanges;
bool gLoaded = false;

std::filesystem::path codepointsPath()
{
    return getAssetDir() / "fonts" / "MaterialIcons-Regular.codepoints";
}

std::string lower(std::string value)
{
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

void ensureLoaded()
{
    if (gLoaded) {
        return;
    }
    gLoaded = true;

    std::ifstream input(codepointsPath());
    if (!input) {
        return;
    }

    std::string name;
    std::string hex;
    ImFontGlyphRangesBuilder builder;
    while (input >> name >> hex) {
        std::uint32_t codepoint = 0;
        try {
            codepoint = static_cast<std::uint32_t>(std::stoul(hex, nullptr, 16));
        } catch (...) {
            continue;
        }

        Icons::IconGlyph glyph(codepoint);
        MaterialIconInfo info;
        info.name = name;
        info.utf8 = glyph.utf8;
        info.lowerName = lower(name);
        info.codepoint = codepoint;
        gIconIndex.emplace(info.name, gIcons.size());
        gIcons.push_back(std::move(info));
        builder.AddChar(static_cast<ImWchar>(codepoint));
    }
    builder.BuildRanges(&gIconRanges);
}

} // namespace

const std::vector<MaterialIconInfo>& materialIconRegistry()
{
    ensureLoaded();
    return gIcons;
}

const MaterialIconInfo* findMaterialIcon(std::string_view name)
{
    ensureLoaded();
    auto it = gIconIndex.find(std::string(name));
    if (it == gIconIndex.end()) {
        return nullptr;
    }
    return &gIcons[it->second];
}

const char* materialIconGlyph(std::string_view name)
{
    if (const MaterialIconInfo* info = findMaterialIcon(name)) {
        return info->utf8.c_str();
    }
    return "";
}

const ImVector<ImWchar>& materialIconGlyphRanges()
{
    ensureLoaded();
    return gIconRanges;
}

} // namespace launcher

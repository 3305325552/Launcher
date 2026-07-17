#pragma once

#include <imgui.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace launcher {

struct MaterialIconInfo {
    std::string name;
    std::string utf8;
    std::string lowerName;
    std::uint32_t codepoint = 0;
};

const std::vector<MaterialIconInfo>& materialIconRegistry();
const MaterialIconInfo* findMaterialIcon(std::string_view name);
const char* materialIconGlyph(std::string_view name);
const ImVector<ImWchar>& materialIconGlyphRanges();

} // namespace launcher

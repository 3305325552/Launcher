#pragma once

#include "launcher/ThemeTypes.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace launcher {

struct UiPalette {
    ImU32 titleBar;
    ImU32 searchBar;
    ImU32 sidebar;
    ImU32 sidebarHover;
    ImU32 sidebarActive;
    ImU32 contentBg;
    ImU32 text;
    ImU32 textMuted;
    ImU32 titleButtonHover;
    ImU32 titleButtonActive;
    ImU32 rippleColor;
    ImVec4 windowBg;
    ImVec4 childBg;
    ImVec4 popupBg;
    ImVec4 border;
    ImVec4 windowOutline;
    ImVec4 popupOutline;
    ImVec4 frameBg;
    ImVec4 frameHovered;
    ImVec4 frameActive;
    ImVec4 button;
    ImVec4 buttonHovered;
    ImVec4 buttonActive;
    ImVec4 header;
    ImVec4 headerHovered;
    ImVec4 headerActive;
    float windowRounding;
    float popupRounding;
    float frameRounding;
    float itemRounding;
    float categoryRounding;
    float sidebarRounding;
    float scrollbarRounding;
    float scrollbarSize;
    float windowPaddingX;
    float windowPaddingY;
    float framePaddingX;
    float framePaddingY;
    float itemSpacingX;
    float itemSpacingY;
    float windowBorderSize;
    float frameBorderSize;
    float windowOutlineSize;
    float popupOutlineSize;
};

inline constexpr float kUiTitleHeight = 48.0f;
inline constexpr float kUiWindowRounding = 8.0f;
inline constexpr float kUiPopupRounding = 7.0f;

bool isDarkTheme(const ThemeDefinition& theme);
UiPalette uiPalette(const ThemeDefinition& theme);
void applyUiStyle(const ThemeDefinition& theme);

inline std::string colorHexFromVec(const ImVec4& color)
{
    const int r = std::clamp(static_cast<int>(color.x * 255.0f + 0.5f), 0, 255);
    const int g = std::clamp(static_cast<int>(color.y * 255.0f + 0.5f), 0, 255);
    const int b = std::clamp(static_cast<int>(color.z * 255.0f + 0.5f), 0, 255);
    const int a = std::clamp(static_cast<int>(color.w * 255.0f + 0.5f), 0, 255);
    char buffer[10]{};
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", r, g, b, a);
    return buffer;
}

} // namespace launcher

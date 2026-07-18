#include "ui/common/UiTheme.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace launcher {
namespace {

const UiPalette kLight{
    IM_COL32(235, 235, 235, 255),
    IM_COL32(225, 225, 225, 255),
    IM_COL32(233, 233, 233, 255),
    IM_COL32(222, 222, 222, 255),
    IM_COL32(210, 210, 210, 255),
    IM_COL32(250, 250, 250, 255),
    IM_COL32(30, 30, 30, 255),
    IM_COL32(115, 115, 115, 255),
    IM_COL32(222, 222, 222, 255),
    IM_COL32(230, 230, 230, 255),
    IM_COL32(88, 140, 214, 110),
    ImVec4(0.98f, 0.98f, 0.98f, 1.0f),
    ImVec4(0.98f, 0.98f, 0.98f, 1.0f),
    ImVec4(0.97f, 0.97f, 0.97f, 0.98f),
    ImVec4(0.78f, 0.78f, 0.78f, 1.0f),
    ImVec4(0.0f, 0.0f, 0.0f, 0.12f),
    ImVec4(0.0f, 0.0f, 0.0f, 0.18f),
    ImVec4(0.90f, 0.90f, 0.90f, 1.0f),
    ImVec4(0.85f, 0.85f, 0.85f, 1.0f),
    ImVec4(0.80f, 0.80f, 0.80f, 1.0f),
    ImVec4(0.84f, 0.84f, 0.84f, 1.0f),
    ImVec4(0.78f, 0.78f, 0.78f, 1.0f),
    ImVec4(0.70f, 0.70f, 0.70f, 1.0f),
    ImVec4(0.80f, 0.80f, 0.80f, 1.0f),
    ImVec4(0.76f, 0.76f, 0.76f, 1.0f),
    ImVec4(0.72f, 0.72f, 0.72f, 1.0f),
    kUiWindowRounding,
    kUiPopupRounding,
    4.0f,
    6.0f,
    6.0f,
    kUiWindowRounding,
    999.0f,
    9.0f,
    0.0f,
    0.0f,
    8.0f,
    4.0f,
    8.0f,
    8.0f,
    0.0f,
    0.0f,
    1.0f,
    1.0f,
};

const UiPalette kDark{
    IM_COL32(34, 34, 34, 255),
    IM_COL32(28, 28, 28, 255),
    IM_COL32(30, 30, 30, 255),
    IM_COL32(42, 42, 42, 255),
    IM_COL32(22, 22, 22, 255),
    IM_COL32(42, 42, 42, 255),
    IM_COL32(224, 224, 224, 255),
    IM_COL32(150, 150, 150, 255),
    IM_COL32(50, 50, 50, 255),
    IM_COL32(58, 58, 58, 255),
    IM_COL32(140, 178, 255, 92),
    ImVec4(0.16f, 0.16f, 0.16f, 1.0f),
    ImVec4(0.16f, 0.16f, 0.16f, 1.0f),
    ImVec4(0.20f, 0.20f, 0.20f, 0.98f),
    ImVec4(0.38f, 0.38f, 0.38f, 1.0f),
    ImVec4(1.0f, 1.0f, 1.0f, 0.12f),
    ImVec4(1.0f, 1.0f, 1.0f, 0.17f),
    ImVec4(0.20f, 0.20f, 0.20f, 1.0f),
    ImVec4(0.26f, 0.26f, 0.26f, 1.0f),
    ImVec4(0.32f, 0.32f, 0.32f, 1.0f),
    ImVec4(0.22f, 0.22f, 0.22f, 1.0f),
    ImVec4(0.30f, 0.30f, 0.30f, 1.0f),
    ImVec4(0.36f, 0.36f, 0.36f, 1.0f),
    ImVec4(0.24f, 0.24f, 0.24f, 1.0f),
    ImVec4(0.30f, 0.30f, 0.30f, 1.0f),
    ImVec4(0.36f, 0.36f, 0.36f, 1.0f),
    kUiWindowRounding,
    kUiPopupRounding,
    4.0f,
    6.0f,
    6.0f,
    kUiWindowRounding,
    999.0f,
    9.0f,
    0.0f,
    0.0f,
    8.0f,
    4.0f,
    8.0f,
    8.0f,
    0.0f,
    0.0f,
    1.0f,
    1.0f,
};

float scalarFromThemeColor(const ThemeColor& color, float minValue, float maxValue)
{
    return std::clamp(color.color[0], minValue, maxValue);
}

ImVec4 vec4FromThemeColor(const ThemeColor& color)
{
    return ImVec4(std::clamp(color.color[0], 0.0f, 1.0f), std::clamp(color.color[1], 0.0f, 1.0f), std::clamp(color.color[2], 0.0f, 1.0f),
                  std::clamp(color.color[3], 0.0f, 1.0f));
}

ImU32 u32FromThemeColor(const ThemeColor& color)
{
    return ImGui::ColorConvertFloat4ToU32(vec4FromThemeColor(color));
}

ImU32 u32FromVec4(const ImVec4& color)
{
    return ImGui::ColorConvertFloat4ToU32(color);
}

float srgbToLinear(float value)
{
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float relativeLuminance(ImU32 color)
{
    const float r = srgbToLinear(static_cast<float>(color & 0xff) / 255.0f);
    const float g = srgbToLinear(static_cast<float>((color >> 8) & 0xff) / 255.0f);
    const float b = srgbToLinear(static_cast<float>((color >> 16) & 0xff) / 255.0f);
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

float contrastRatio(ImU32 foreground, ImU32 background)
{
    const float fg = relativeLuminance(foreground);
    const float bg = relativeLuminance(background);
    const float lighter = std::max(fg, bg);
    const float darker = std::min(fg, bg);
    return (lighter + 0.05f) / (darker + 0.05f);
}

ImU32 strongTextForBackground(ImU32 background)
{
    constexpr ImU32 darkText = IM_COL32(30, 30, 30, 255);
    constexpr ImU32 lightText = IM_COL32(232, 232, 232, 255);
    return contrastRatio(darkText, background) >= contrastRatio(lightText, background) ? darkText : lightText;
}

ImU32 mutedTextForBackground(ImU32 background)
{
    const ImU32 muted = relativeLuminance(background) > 0.45f ? IM_COL32(105, 105, 105, 255) : IM_COL32(168, 168, 168, 255);
    return contrastRatio(muted, background) >= 2.2f ? muted : strongTextForBackground(background);
}

bool readableOnMainSurfaces(ImU32 text, const UiPalette& palette, float minContrast)
{
    return contrastRatio(text, palette.contentBg) >= minContrast && contrastRatio(text, u32FromVec4(palette.windowBg)) >= minContrast &&
           contrastRatio(text, u32FromVec4(palette.childBg)) >= minContrast;
}

void ensureLightPaletteReadability(UiPalette& palette)
{
    if (!readableOnMainSurfaces(palette.text, palette, 3.2f)) {
        palette.text = strongTextForBackground(palette.contentBg);
    }
    if (!readableOnMainSurfaces(palette.textMuted, palette, 2.2f)) {
        palette.textMuted = mutedTextForBackground(palette.contentBg);
    }
}

bool keyIs(std::string_view key, std::string_view a)
{
    return key == a;
}

bool keyIs(std::string_view key, std::string_view a, std::string_view b)
{
    return key == a || key == b;
}

bool keyIs(std::string_view key, std::string_view a, std::string_view b, std::string_view c)
{
    return key == a || key == b || key == c;
}

void applyThemeColor(UiPalette& palette, const ThemeColor& color)
{
    const std::string_view key(color.key);
    const ImVec4 value = vec4FromThemeColor(color);
    const ImU32 packed = u32FromThemeColor(color);

    if (keyIs(key, "global_text", "text"))
        palette.text = packed;
    else if (keyIs(key, "global_text_disabled", "global_textDisabled", "text_disabled"))
        palette.textMuted = packed;
    else if (keyIs(key, "global_separator", "window_border", "table_border"))
        palette.border = value;
    else if (keyIs(key, "top_background", "top_bg", "title_bar"))
        palette.titleBar = packed;
    else if (keyIs(key, "top_text", "top_button_text_nil", "top_button_text_hover"))
        palette.text = packed;
    else if (keyIs(key, "top_button_hover", "top_btn_bj_hover", "title_button_hover"))
        palette.titleButtonHover = packed;
    else if (keyIs(key, "top_button_active", "top_btn_bj_active", "title_button_active"))
        palette.titleButtonActive = packed;
    else if (keyIs(key, "search_background", "search_bg", "input_search_bg"))
        palette.searchBar = packed;
    else if (keyIs(key, "search_placeholder", "input_text_hint", "search_result_empty"))
        palette.textMuted = packed;
    else if (keyIs(key, "search_result_hover", "search_result_background"))
        palette.headerHovered = value;
    else if (keyIs(key, "search_result_active", "search_result_selected"))
        palette.headerActive = value;
    else if (keyIs(key, "left_background", "left_bg", "category_background"))
        palette.sidebar = packed;
    else if (keyIs(key, "tab_text_nil", "tab_text_hover", "tab_text_active"))
        palette.text = packed;
    else if (keyIs(key, "tab_background_hover", "tab_bg_hover", "category_hover"))
        palette.sidebarHover = packed;
    else if (keyIs(key, "tab_background_active", "tab_bg_active", "category_active"))
        palette.sidebarActive = packed;
    else if (keyIs(key, "content_background", "content_bg", "main_background"))
        palette.contentBg = packed;
    else if (keyIs(key, "window_background", "window_bg"))
        palette.windowBg = value;
    else if (keyIs(key, "child_background", "child_bg"))
        palette.childBg = value;
    else if (keyIs(key, "popup_background", "menu_background", "tooltip_background"))
        palette.popupBg = value;
    else if (keyIs(key, "border", "window_border", "menu_border"))
        palette.border = value;
    else if (keyIs(key, "window_outline"))
        palette.windowOutline = value;
    else if (keyIs(key, "popup_outline", "menu_outline", "tooltip_outline"))
        palette.popupOutline = value;
    else if (keyIs(key, "frame_background", "input_background", "select_background"))
        palette.frameBg = value;
    else if (keyIs(key, "frame_background_hover", "input_background_hover", "select_background_hover"))
        palette.frameHovered = value;
    else if (keyIs(key, "frame_background_active", "input_background_active", "select_background_active"))
        palette.frameActive = value;
    else if (keyIs(key, "button_text", "menu_text", "tooltip_text"))
        palette.text = packed;
    else if (keyIs(key, "menu_text_disabled", "tooltip_separator"))
        palette.textMuted = packed;
    else if (keyIs(key, "button_background", "button_bg"))
        palette.button = value;
    else if (keyIs(key, "button_background_hover", "button_bg_hover"))
        palette.buttonHovered = value;
    else if (keyIs(key, "button_background_active", "button_bg_active"))
        palette.buttonActive = value;
    else if (keyIs(key, "header_background", "header_bg", "menu_hover"))
        palette.header = value;
    else if (keyIs(key, "header_background_hover", "header_bg_hover", "item_background_hover"))
        palette.headerHovered = value;
    else if (keyIs(key, "header_background_active", "header_bg_active", "item_background_active"))
        palette.headerActive = value;
    else if (keyIs(key, "menu_active", "item_nav_background_active", "item_border_active"))
        palette.headerActive = value;
    else if (keyIs(key, "table_header_background", "theme_editor_group_background"))
        palette.header = value;
    else if (keyIs(key, "table_row_even", "theme_editor_row_even"))
        palette.childBg = value;
    else if (keyIs(key, "table_row_odd", "theme_editor_row_odd"))
        palette.windowBg = value;
    else if (keyIs(key, "ripple_color"))
        palette.rippleColor = packed;
    else if (keyIs(key, "window_rounding"))
        palette.windowRounding = scalarFromThemeColor(color, 0.0f, 24.0f);
    else if (keyIs(key, "popup_rounding"))
        palette.popupRounding = scalarFromThemeColor(color, 0.0f, 24.0f);
    else if (keyIs(key, "frame_rounding"))
        palette.frameRounding = scalarFromThemeColor(color, 0.0f, 16.0f);
    else if (keyIs(key, "item_rounding"))
        palette.itemRounding = scalarFromThemeColor(color, 0.0f, 20.0f);
    else if (keyIs(key, "category_rounding"))
        palette.categoryRounding = scalarFromThemeColor(color, 0.0f, 20.0f);
    else if (keyIs(key, "sidebar_rounding"))
        palette.sidebarRounding = scalarFromThemeColor(color, 0.0f, 24.0f);
    else if (keyIs(key, "scrollbar_rounding"))
        palette.scrollbarRounding = scalarFromThemeColor(color, 0.0f, 999.0f);
    else if (keyIs(key, "scrollbar_size"))
        palette.scrollbarSize = scalarFromThemeColor(color, 2.0f, 28.0f);
    else if (keyIs(key, "window_padding_x"))
        palette.windowPaddingX = scalarFromThemeColor(color, 0.0f, 40.0f);
    else if (keyIs(key, "window_padding_y"))
        palette.windowPaddingY = scalarFromThemeColor(color, 0.0f, 40.0f);
    else if (keyIs(key, "frame_padding_x"))
        palette.framePaddingX = scalarFromThemeColor(color, 0.0f, 32.0f);
    else if (keyIs(key, "frame_padding_y"))
        palette.framePaddingY = scalarFromThemeColor(color, 0.0f, 24.0f);
    else if (keyIs(key, "item_spacing_x"))
        palette.itemSpacingX = scalarFromThemeColor(color, 0.0f, 32.0f);
    else if (keyIs(key, "item_spacing_y"))
        palette.itemSpacingY = scalarFromThemeColor(color, 0.0f, 32.0f);
    else if (keyIs(key, "window_border_size"))
        palette.windowBorderSize = scalarFromThemeColor(color, 0.0f, 4.0f);
    else if (keyIs(key, "frame_border_size"))
        palette.frameBorderSize = scalarFromThemeColor(color, 0.0f, 4.0f);
    else if (keyIs(key, "window_outline_size"))
        palette.windowOutlineSize = scalarFromThemeColor(color, 0.0f, 4.0f);
    else if (keyIs(key, "popup_outline_size", "menu_outline_size"))
        palette.popupOutlineSize = scalarFromThemeColor(color, 0.0f, 4.0f);
}

void applyPaletteToImGui(bool dark, const UiPalette& palette)
{
    if (dark) {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(palette.windowPaddingX, palette.windowPaddingY);
    style.FramePadding = ImVec2(palette.framePaddingX, palette.framePaddingY);
    style.ItemSpacing = ImVec2(palette.itemSpacingX, palette.itemSpacingY);
    style.WindowRounding = palette.windowRounding;
    style.ChildRounding = palette.frameRounding;
    style.FrameRounding = palette.frameRounding;
    style.PopupRounding = palette.popupRounding;
    style.ScrollbarRounding = palette.scrollbarRounding;
    style.ScrollbarSize = palette.scrollbarSize;
    style.GrabRounding = palette.frameRounding;
    style.WindowBorderSize = std::max(palette.windowBorderSize, palette.windowOutlineSize);
    style.PopupBorderSize = palette.popupOutlineSize;
    style.FrameBorderSize = palette.frameBorderSize;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(palette.text);
    colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(palette.textMuted);
    colors[ImGuiCol_WindowBg] = palette.windowBg;
    colors[ImGuiCol_ChildBg] = palette.childBg;
    colors[ImGuiCol_PopupBg] = palette.popupBg;
    colors[ImGuiCol_Border] = palette.border;
    colors[ImGuiCol_FrameBg] = palette.frameBg;
    colors[ImGuiCol_FrameBgHovered] = palette.frameHovered;
    colors[ImGuiCol_FrameBgActive] = palette.frameActive;
    colors[ImGuiCol_Button] = palette.button;
    colors[ImGuiCol_ButtonHovered] = palette.buttonHovered;
    colors[ImGuiCol_ButtonActive] = palette.buttonActive;
    colors[ImGuiCol_Header] = palette.header;
    colors[ImGuiCol_HeaderHovered] = palette.headerHovered;
    colors[ImGuiCol_HeaderActive] = palette.headerActive;
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ScrollbarGrab] = palette.frameHovered;
    colors[ImGuiCol_ScrollbarGrabHovered] = palette.frameActive;
    colors[ImGuiCol_ScrollbarGrabActive] = palette.buttonActive;
}

} // namespace

bool isDarkTheme(const ThemeDefinition& theme)
{
    return theme.dark;
}

const UiPalette& fallbackPalette(bool dark)
{
    return dark ? kDark : kLight;
}

UiPalette uiPalette(const ThemeDefinition& theme)
{
    UiPalette palette = fallbackPalette(theme.dark);
    for (const ThemeColor& color : theme.colors) {
        applyThemeColor(palette, color);
    }
    if (!theme.dark) {
        ensureLightPaletteReadability(palette);
    }
    return palette;
}

void applyUiStyle(const ThemeDefinition& theme)
{
    applyPaletteToImGui(theme.dark, uiPalette(theme));
}

} // namespace launcher

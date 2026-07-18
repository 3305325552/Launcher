#include "ui/settings/SettingsWidgets.hpp"

#include "ui/common/Localization.hpp"
#include "ui/common/UiChrome.hpp"
#include "ui/common/UiAnimation.hpp"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace launcher::settings_ui {
namespace {

std::string vkName(int vk)
{
    if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
    if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    switch (vk) {
    case VK_SPACE: return "Space";
    case VK_ESCAPE: return "Esc";
    case VK_TAB: return "Tab";
    case VK_RETURN: return "Enter";
    case VK_BACK: return "Backspace";
    case VK_DELETE: return "Delete";
    case VK_INSERT: return "Insert";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    default: return {};
    }
}

bool isModifierVk(int vk)
{
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
           vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LWIN || vk == VK_RWIN;
}

} // namespace

UiPalette currentControlPalette()
{
    UiPalette palette = uiPalette(ThemeDefinition{});
    ImGuiStyle& style = ImGui::GetStyle();
    palette.text = ImGui::GetColorU32(ImGuiCol_Text);
    palette.textMuted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    palette.popupBg = style.Colors[ImGuiCol_PopupBg];
    palette.border = style.Colors[ImGuiCol_Border];
    palette.popupOutline = style.Colors[ImGuiCol_Border];
    palette.header = style.Colors[ImGuiCol_Header];
    palette.headerHovered = style.Colors[ImGuiCol_HeaderHovered];
    palette.headerActive = style.Colors[ImGuiCol_HeaderActive];
    palette.frameRounding = style.FrameRounding;
    palette.popupRounding = style.PopupRounding;
    palette.itemRounding = std::max(4.0f, style.FrameRounding);
    palette.popupOutlineSize = style.PopupBorderSize;
    return palette;
}

bool beginCombo(const UiPalette& palette, const char* id, const char* preview)
{
    return beginStyledCombo(palette, id, preview);
}

void endCombo(bool open)
{
    endStyledCombo(open);
}

bool comboItem(const UiPalette& palette, const char* label, bool selected)
{
    return styledComboItem(palette, label, selected);
}

void section(const char* title)
{
    ImGui::Dummy(ImVec2(1.0f, 14.0f));
    ImGui::TextUnformatted(title);
    ImGui::Dummy(ImVec2(1.0f, 6.0f));
}

void helpMarker(const char* text)
{
    if (!text || text[0] == '\0') {
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        beginStyledTooltip(ImGui::GetID(text));
        ImGui::TextUnformatted(text);
        endStyledTooltip();
    }
}

bool rowCheckbox(const char* label, bool* value, float indent, const char* tooltip)
{
    ImGui::Indent(indent);
    const bool changed = ui_anim::checkbox(label, value);
    helpMarker(tooltip);
    ImGui::Unindent(indent);
    return changed;
}

bool rowSliderInt(const char* label, int* value, int min, int max, const char* format, float width, const char* tooltip)
{
    ImGui::Indent(28.0f);
    ImGui::TextUnformatted(label);
    helpMarker(tooltip);
    ImGui::SameLine(400.0f);
    ImGui::SetNextItemWidth(width);
    const bool changed = ui_anim::sliderInt((std::string("##") + label).c_str(), value, min, max, format);
    ImGui::Unindent(28.0f);
    return changed;
}

bool rowCombo(const char* label, int* value, const char* items, float width, const char* tooltip)
{
    const UiPalette palette = currentControlPalette();
    ImGui::Indent(28.0f);
    ImGui::TextUnformatted(label);
    helpMarker(tooltip);
    ImGui::SameLine(400.0f);
    ImGui::SetNextItemWidth(width);
    std::vector<const char*> itemList;
    for (const char* item = items; item && item[0] != '\0'; item += std::strlen(item) + 1) {
        itemList.push_back(item);
    }
    if (itemList.empty()) {
        ImGui::Button("##empty-combo", ImVec2(width, 30.0f));
        ImGui::Unindent(28.0f);
        return false;
    }
    *value = std::clamp(*value, 0, static_cast<int>(itemList.size()) - 1);
    bool changed = false;
    const std::string id = std::string("##") + label;
    const bool comboOpen = beginCombo(palette, id.c_str(), itemList[*value]);
    if (comboOpen) {
        ui_anim::pushAppearAlpha(ImGui::GetID((std::string("combo-") + label).c_str()), 0.10f, 0.20f);
        for (int i = 0; i < static_cast<int>(itemList.size()); ++i) {
            if (comboItem(palette, itemList[i], i == *value)) {
                *value = i;
                changed = true;
            }
        }
        ui_anim::popAppearAlpha();
    }
    endCombo(comboOpen);
    ImGui::Unindent(28.0f);
    return changed;
}

bool drawHotkeyInput(const char* id, std::string& hotkey)
{
    static std::string captureId;
    static int captureStartFrame = 0;
    static std::array<bool, 256> initiallyDown{};
    bool changed = false;

    bool capturing = captureId == id;
    const std::string label = (capturing ? "Press..." : (hotkey.empty() ? tr("Not set") : hotkey)) + std::string("##hotkey-button-") + id;
    if (ui_anim::button(label.c_str(), ImVec2(220.0f, 30.0f))) {
        captureId = id;
        captureStartFrame = ImGui::GetFrameCount();
        for (int vk = 0; vk < static_cast<int>(initiallyDown.size()); ++vk) {
            initiallyDown[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
        }
        capturing = true;
    }
    if (!capturing || ImGui::GetFrameCount() == captureStartFrame) {
        return changed;
    }
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
        captureId.clear();
        return changed;
    }

    for (int vk = VK_BACK; vk < 255; ++vk) {
        if (isModifierVk(vk) || (vk >= VK_F13 && vk <= VK_F24)) {
            continue;
        }
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (!down) {
            initiallyDown[vk] = false;
            continue;
        }
        if (initiallyDown[vk]) {
            continue;
        }
        std::string key = vkName(vk);
        if (key.empty()) {
            continue;
        }
        std::string value;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) value += "Ctrl+";
        if (GetAsyncKeyState(VK_MENU) & 0x8000) value += "Alt+";
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) value += "Shift+";
        if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) value += "Win+";
        value += key;
        hotkey = value;
        captureId.clear();
        changed = true;
        break;
    }
    return changed;
}

} // namespace launcher::settings_ui

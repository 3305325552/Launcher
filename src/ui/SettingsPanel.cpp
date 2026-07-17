#include "ui/SettingsPanel.hpp"

#include "app/AppContext.hpp"
#include "platform/SystemIntegration.hpp"
#include "ui/ConfigTransfer.hpp"
#include "ui/Localization.hpp"
#include "ui/MainDockChrome.hpp"
#include "ui/MaterialIcons.hpp"
#include "ui/UiAnimation.hpp"
#include "ui/UiTheme.hpp"
#include "ui/UserGuideView.hpp"

#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cstdio>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace launcher {
namespace {

bool gSettingsWasVisible = false;
bool gSettingsNeedsFocus = false;
HWND gSettingsHwnd = nullptr;
bool gSettingsLayerKnown = false;
bool gSettingsTopmost = false;
int gPendingSettingsTab = -1;
std::string gSettingsSearch;
bool gSettingsSearchOpen = false;
bool gSettingsSearchNeedsFocus = false;
int gSettingsTrimFramesRemaining = 0;

struct SettingsSearchEntry {
    int tab = 0;
    const char* title = "";
    const char* detail = "";
};

void applySettingsStyle(const AppContext& context)
{
    const UiPalette palette = uiPalette(context.themes.active());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(palette.framePaddingX, palette.framePaddingY));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, palette.windowRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, palette.popupRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, palette.windowOutlineSize);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, palette.popupOutlineSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, palette.windowBg);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.childBg);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette.text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(palette.textMuted));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, palette.frameBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, palette.frameHovered);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, palette.frameActive);
    ImGui::PushStyleColor(ImGuiCol_Button, palette.button);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.buttonHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.buttonActive);
    ImGui::PushStyleColor(ImGuiCol_Header, palette.header);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, palette.headerHovered);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, palette.headerActive);
    ImGui::PushStyleColor(ImGuiCol_Border, palette.windowOutline);
}

void popSettingsStyle()
{
    ImGui::PopStyleColor(14);
    ImGui::PopStyleVar(6);
}

void trimSettingsWorkingSet()
{
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

ImU32 readableTextColor(ImU32 background)
{
    const float r = static_cast<float>(background & 0xff) / 255.0f;
    const float g = static_cast<float>((background >> 8) & 0xff) / 255.0f;
    const float b = static_cast<float>((background >> 16) & 0xff) / 255.0f;
    const float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    return luminance > 0.58f ? IM_COL32(42, 42, 42, 255) : IM_COL32(255, 255, 255, 255);
}

UiPalette currentSettingsControlPalette()
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

void pushSettingsPopupStyle(const UiPalette& palette)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, palette.popupRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, palette.popupOutlineSize);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, palette.frameRounding);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, palette.popupBg);
    ImGui::PushStyleColor(ImGuiCol_Border, palette.popupOutline);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette.text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(palette.textMuted));
    ImGui::PushStyleColor(ImGuiCol_Header, palette.header);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, palette.headerHovered);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, palette.headerActive);
}

void popSettingsPopupStyle()
{
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(5);
}

bool beginSettingsCombo(const UiPalette& palette, const char* id, const char* preview)
{
    return beginStyledCombo(palette, id, preview);
}

void endSettingsCombo(bool open)
{
    endStyledCombo(open);
}

bool settingsComboItem(const UiPalette& palette, const char* label, bool selected)
{
    return styledComboItem(palette, label, selected);
}

std::string openImageFileDialog()
{
    wchar_t file[MAX_PATH]{};
    const std::wstring title = trw("Select Background Image");
    const std::wstring filter = fileDialogFilter({{"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp"}, {"All Files", L"*.*"}});
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    if (ImGuiViewport* viewport = ImGui::GetWindowViewport()) {
        ofn.hwndOwner = static_cast<HWND>(viewport->PlatformHandleRaw);
    }
    ofn.lpstrTitle = title.c_str();
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_ENABLESIZING | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) {
        return {};
    }
    return narrow(file);
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

bool rowCheckbox(const char* label, bool* value, float indent = 28.0f, const char* tooltip = nullptr)
{
    ImGui::Indent(indent);
    const bool changed = ui_anim::checkbox(label, value);
    helpMarker(tooltip);
    ImGui::Unindent(indent);
    return changed;
}

bool rowSliderInt(const char* label, int* value, int min, int max, const char* format = "%d", float width = 220.0f,
                  const char* tooltip = nullptr)
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

bool rowCombo(const char* label, int* value, const char* items, float width = 220.0f, const char* tooltip = nullptr)
{
    const UiPalette palette = currentSettingsControlPalette();
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
    const bool comboOpen = beginSettingsCombo(palette, id.c_str(), itemList[*value]);
    if (comboOpen) {
        ui_anim::pushAppearAlpha(ImGui::GetID((std::string("combo-") + label).c_str()), 0.10f, 0.20f);
        for (int i = 0; i < static_cast<int>(itemList.size()); ++i) {
            if (settingsComboItem(palette, itemList[i], i == *value)) {
                *value = i;
                changed = true;
            }
        }
        ui_anim::popAppearAlpha();
    }
    endSettingsCombo(comboOpen);
    ImGui::Unindent(28.0f);
    return changed;
}

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
    if (!capturing) {
        return changed;
    }

    if (ImGui::GetFrameCount() == captureStartFrame) {
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

void drawSidebar(AppContext& context, int& tab)
{
    const UiPalette palette = uiPalette(context.themes.active());
    const std::array<const char*, 7> tabs = {tr("General"), tr("Actions"), tr("Interface"), tr("Search"),
                                             tr("Plugins"), tr("Help"),    tr("About")};

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(start, ImVec2(start.x + 145.0f, start.y + ImGui::GetContentRegionAvail().y), palette.sidebar, palette.sidebarRounding,
                      ImDrawFlags_RoundCornersBottomLeft);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("settings-sidebar", ImVec2(145.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        ImGui::PushID(i);
        const bool selected = tab == i;
        const ImVec2 row = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("tab", ImVec2(145.0f, 44.0f))) {
            tab = i;
        }
        const bool hovered = ImGui::IsItemHovered();
        if (selected || hovered) {
            dl->AddRectFilled(row, ImVec2(row.x + 145.0f, row.y + 44.0f), selected ? palette.sidebarActive : palette.sidebarHover,
                              palette.categoryRounding);
        }
        ui_anim::rippleLastItem(palette, 0.0f);
        dl->AddText(ImVec2(row.x + 44.0f, row.y + 12.0f), palette.text, tabs[i]);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void tag(const char* text, const ImVec4& color)
{
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
    ImGui::Button(text, ImVec2(0.0f, 24.0f));
    ImGui::PopStyleColor(3);
}

void drawBadge(ImDrawList* dl, ImVec2& pos, const char* text, ImU32 color)
{
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 size(textSize.x + 12.0f, 23.0f);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), color, 4.0f);
    dl->AddText(ImVec2(pos.x + 6.0f, pos.y + 3.0f), readableTextColor(color), text);
    pos.x += size.x + 6.0f;
}

std::string joinPluginCapabilities(const std::vector<std::string>& capabilities)
{
    std::string text;
    for (const std::string& capability : capabilities) {
        if (!text.empty()) {
            text += ", ";
        }
        text += capability;
    }
    return text.empty() ? "-" : text;
}

bool settingStringIsTrue(const std::string& value)
{
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

bool settingsPopupItem(const UiPalette& palette, const char* label)
{
    ImGui::PushID(label);
    const float rowHeight = std::max(ImGui::GetFrameHeight(), ImGui::CalcTextSize(label).y + 10.0f);
    const float rowWidth = std::max(124.0f, ImGui::GetContentRegionAvail().x);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    const bool clicked = ImGui::Selectable("##popup-item", false, ImGuiSelectableFlags_None, ImVec2(rowWidth, rowHeight));
    ImGui::PopStyleColor(3);

    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    if (hovered || active) {
        ImVec4 color = active ? palette.headerActive : palette.headerHovered;
        color.w = std::max(color.w, active ? 0.70f : 0.62f);
        ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(color), palette.itemRounding);
    }
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 8.0f, min.y + (rowHeight - textSize.y) * 0.5f), palette.text, label);
    ImGui::PopID();
    return clicked;
}

bool drawPluginSettingField(AppContext& context, const PluginInfo& plugin, const PluginSettingField& field)
{
    std::map<std::string, std::string>& values = context.plugins.settingsFor(context.persisted().settings, plugin.id);
    std::string& value = values[field.id];
    if (value.empty() && !field.defaultValue.empty()) {
        value = field.defaultValue;
    }

    bool changed = false;
    const UiPalette palette = currentSettingsControlPalette();
    ImGui::PushID(field.id.c_str());
    ImGui::Indent(22.0f);
    ImGui::TextUnformatted(field.label.empty() ? field.id.c_str() : field.label.c_str());
    ImGui::SameLine(220.0f);
    ImGui::SetNextItemWidth(260.0f);

    if (field.type == "bool" || field.type == "checkbox") {
        bool enabled = settingStringIsTrue(value);
        if (ImGui::Checkbox("##value", &enabled)) {
            value = enabled ? "true" : "false";
            changed = true;
        }
    } else if (field.type == "choice" || field.type == "combo" || field.type == "select") {
        if (field.choices.empty()) {
            changed |= ImGui::InputText("##value", &value);
        } else {
            std::string preview = value.empty() ? field.choices.front() : value;
            const bool comboOpen = beginSettingsCombo(palette, "##value", preview.c_str());
            if (comboOpen) {
                ui_anim::pushAppearAlpha(ImGui::GetID("combo-plugin-setting"), 0.10f, 0.20f);
                for (const std::string& choice : field.choices) {
                    if (settingsComboItem(palette, choice.c_str(), choice == value)) {
                        value = choice;
                        changed = true;
                    }
                }
                ui_anim::popAppearAlpha();
            }
            endSettingsCombo(comboOpen);
        }
    } else if (field.type == "number" || field.type == "int" || field.type == "float") {
        double number = value.empty() ? std::strtod(field.defaultValue.c_str(), nullptr) : std::strtod(value.c_str(), nullptr);
        if (ImGui::InputDouble("##value", &number, 0.0, 0.0, "%.6g")) {
            value = std::to_string(number);
            changed = true;
        }
    } else {
        ImGuiInputTextFlags flags = field.type == "password" ? ImGuiInputTextFlags_Password : ImGuiInputTextFlags_None;
        changed |= ImGui::InputText("##value", &value, flags);
    }
    ImGui::Unindent(22.0f);
    ImGui::PopID();
    return changed;
}

std::string lowercaseAscii(std::string value)
{
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

bool settingsSearchMatch(const SettingsSearchEntry& entry, const std::string& query)
{
    if (query.empty()) {
        return true;
    }
    const std::string haystack = lowercaseAscii(std::string(entry.title) + " " + entry.detail);
    return haystack.find(query) != std::string::npos;
}

bool drawSettingsSearchResults(int& activeTab)
{
    const std::string query = lowercaseAscii(gSettingsSearch);
    const std::array entries = {
        SettingsSearchEntry{0, tr("Language"), tr("General")},
        SettingsSearchEntry{0, tr("Start with Windows"), tr("General")},
        SettingsSearchEntry{0, tr("Global wake hotkey"), tr("General")},
        SettingsSearchEntry{0, tr("Automatically check for updates"), tr("General")},
        SettingsSearchEntry{0, tr("Check for updates at startup"), tr("General")},
        SettingsSearchEntry{0, tr("Check for updates daily"), tr("General")},
        SettingsSearchEntry{0, tr("Config Directory"), tr("General")},
        SettingsSearchEntry{0, tr("Import Config"), tr("General")},
        SettingsSearchEntry{0, tr("Export Config"), tr("General")},
        SettingsSearchEntry{1, tr("Double-click to run item"), tr("Actions")},
        SettingsSearchEntry{1, tr("Run all in virtual folders"), tr("Actions")},
        SettingsSearchEntry{1, tr("After running item"), tr("Actions")},
        SettingsSearchEntry{1, tr("Shift + right-click opens Explorer menu"), tr("Actions")},
        SettingsSearchEntry{1, tr("Explorer directory search menu"), tr("Actions")},
        SettingsSearchEntry{1, tr("Drag blank area to move window"), tr("Actions")},
        SettingsSearchEntry{2, tr("Theme"), tr("Interface")},
        SettingsSearchEntry{2, tr("Theme Editor"), tr("Interface")},
        SettingsSearchEntry{2, tr("Animation speed"), tr("Interface")},
        SettingsSearchEntry{2, tr("Item icon size"), tr("Interface")},
        SettingsSearchEntry{2, tr("Tooltip"), tr("Interface")},
        SettingsSearchEntry{3, tr("Smooth input"), tr("Search")},
        SettingsSearchEntry{3, tr("Search delay"), tr("Search")},
        SettingsSearchEntry{3, tr("Enable Global Search"), tr("Search")},
        SettingsSearchEntry{3, tr("Global search scan intensity"), tr("Search")},
        SettingsSearchEntry{3, tr("Use default icons"), tr("Search")},
        SettingsSearchEntry{3, tr("Enable Advanced Search"), tr("Search")},
        SettingsSearchEntry{3, tr("Regular expression"), tr("Search")},
        SettingsSearchEntry{3, tr("Pinyin"), tr("Search")},
        SettingsSearchEntry{3, tr("Search Scope"), tr("Search")},
        SettingsSearchEntry{4, tr("Installed Extensions"), tr("Plugins")},
        SettingsSearchEntry{4, tr("Reload"), tr("Plugins")},
        SettingsSearchEntry{4, "Sample Echo Plugin", tr("Plugins")},
        SettingsSearchEntry{4, "Sample Calculator Plugin", tr("Plugins")},
        SettingsSearchEntry{4, "Sample Color Plugin", tr("Plugins")},
        SettingsSearchEntry{4, "Sample CLI Preview Plugin", tr("Plugins")},
        SettingsSearchEntry{5, tr("Help"), tr("User Guide")},
        SettingsSearchEntry{5, tr("Hotkeys"), tr("User Guide")},
        SettingsSearchEntry{5, tr("Virtual Directories"), tr("User Guide")},
        SettingsSearchEntry{5, tr("Parameter Variables"), tr("User Guide")},
        SettingsSearchEntry{6, tr("About"), tr("Launcher")},
        SettingsSearchEntry{6, tr("Runtime: Windows 10+"), tr("About")},
        SettingsSearchEntry{6, tr("Tech stack: C++ / ImGui"), tr("About")},
    };

    ImGui::TextDisabled("%s", tr("Search results"));
    ImGui::Dummy(ImVec2(1.0f, 8.0f));
    int count = 0;
    for (const SettingsSearchEntry& entry : entries) {
        if (!settingsSearchMatch(entry, query)) {
            continue;
        }
        ImGui::PushID(count);
        if (ImGui::Selectable(entry.title, false, ImGuiSelectableFlags_None, ImVec2(0.0f, 34.0f))) {
            activeTab = entry.tab;
            gSettingsSearch.clear();
        }
        ImGui::SameLine(320.0f);
        ImGui::TextDisabled("%s", entry.detail);
        ImGui::PopID();
        ++count;
    }
    if (count == 0) {
        ImGui::TextDisabled("%s", tr("No results"));
    }
    return false;
}

void clearSettingsSearch()
{
    gSettingsSearch.clear();
    gSettingsSearchOpen = false;
    gSettingsSearchNeedsFocus = false;
}

bool drawSettingsTitleBar(const UiPalette& palette)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowWidth();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + kUiTitleHeight), palette.titleBar, palette.windowRounding,
                      ImDrawFlags_RoundCornersTop);
    dl->AddText(ImVec2(pos.x + 14.0f, pos.y + 13.0f), palette.text, "Settings");

    constexpr float buttonWidth = 58.0f;
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("settings-drag", ImVec2(std::max(0.0f, width - buttonWidth * 2.0f), kUiTitleHeight));
    if (ImGui::IsItemActivated()) {
        ImGui::StartMouseMovingWindow(ImGui::GetCurrentWindow());
    }

    if (drawTitleButton(palette, kUiTitleHeight, "settings-search-button", ImVec2(pos.x + width - buttonWidth * 2.0f, pos.y), Icons::Search,
                        gSettingsSearchOpen)) {
        gSettingsSearchOpen = !gSettingsSearchOpen;
        if (gSettingsSearchOpen) {
            gSettingsSearchNeedsFocus = true;
        } else {
            gSettingsSearch.clear();
        }
    }
    return drawTitleButton(palette, kUiTitleHeight, "settings-close-button", ImVec2(pos.x + width - buttonWidth, pos.y), Icons::Close);
}

bool drawPluginCard(const PluginInfo& plugin, AppContext& context)
{
    const UiPalette palette = uiPalette(context.themes.active());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = std::max(360.0f, ImGui::GetContentRegionAvail().x - 8.0f);
    const float settingsHeight = plugin.settingsSchema.empty() ? 0.0f : 34.0f + static_cast<float>(plugin.settingsSchema.size()) * 34.0f;
    const float errorHeight = plugin.loadError.empty() ? 0.0f : 26.0f;
    const float height = 146.0f + settingsHeight + errorHeight;
    const bool dark = context.themes.active().dark;
    const ImU32 cardBg = dark ? IM_COL32(48, 48, 48, 255) : IM_COL32(246, 246, 246, 255);
    const ImU32 text = palette.text;
    const ImU32 muted = palette.textMuted;
    const ImU32 border = ImGui::ColorConvertFloat4ToU32(palette.border);

    ImGui::PushID(plugin.id.c_str());
    dl->AddRectFilled(start, ImVec2(start.x + width, start.y + height), cardBg, 7.0f);
    dl->AddRect(start, ImVec2(start.x + width, start.y + height), border, 7.0f);

    bool changed = false;
    const std::string title = plugin.name.empty() ? plugin.id : plugin.name;
    const float controlLeft = start.x + width - 90.0f;
    const ImVec4 titleClip(start.x + 14.0f, start.y, controlLeft - 10.0f, start.y + 34.0f);
    dl->AddText(nullptr, 0.0f, ImVec2(start.x + 14.0f, start.y + 12.0f), text, title.c_str(), nullptr, 0.0f, &titleClip);
    ImGui::SetCursorScreenPos(ImVec2(controlLeft, start.y + 10.0f));
    bool enabled = plugin.enabled;
    if (ImGui::Checkbox("##enabled", &enabled)) {
        context.plugins.setEnabled(context.persisted().settings, plugin.id, enabled);
        changed = true;
    }
    ImGui::SameLine();
    if (ui_anim::button("...", ImVec2(34.0f, 26.0f))) {
        ImGui::OpenPopup("plugin-menu");
    }
    const UiPalette popupPalette = currentSettingsControlPalette();
    pushSettingsPopupStyle(popupPalette);
    const bool animatedPopup = ui_anim::pushPopupAppear("plugin-menu");
    if (ImGui::BeginPopup("plugin-menu")) {
        suppressCurrentViewportNativeBorder();
        if (settingsPopupItem(popupPalette, enabled ? tr("Disable") : tr("Enable"))) {
            enabled = !enabled;
            context.plugins.setEnabled(context.persisted().settings, plugin.id, enabled);
            changed = true;
        }
        if (settingsPopupItem(popupPalette, tr("Open Plugin Folder"))) {
            ShellExecuteW(nullptr, L"open", plugin.directory.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::EndPopup();
    }
    if (animatedPopup) {
        ui_anim::popAppearAlpha();
    }
    popSettingsPopupStyle();

    ImVec2 badgePos(start.x + 14.0f, start.y + 42.0f);
    drawBadge(dl, badgePos, plugin.lifecycle.c_str(), IM_COL32(72, 174, 235, 255));
    for (const std::string& capability : plugin.capabilities) {
        drawBadge(dl, badgePos, capability.c_str(), dark ? IM_COL32(105, 105, 105, 255) : IM_COL32(178, 178, 178, 255));
    }

    const float left = start.x + 14.0f;
    const float right = start.x + width * 0.52f;
    const float y1 = start.y + 74.0f;
    const float y2 = start.y + 98.0f;
    dl->AddText(ImVec2(left, y1), muted, "Author:");
    dl->AddText(ImVec2(left + 64.0f, y1), text, plugin.author.empty() ? "-" : plugin.author.c_str());
    dl->AddText(ImVec2(left, y2), muted, "Version:");
    dl->AddText(ImVec2(left + 64.0f, y2), text, plugin.version.empty() ? "-" : plugin.version.c_str());
    dl->AddText(ImVec2(right, y1), muted, "ID:");
    dl->AddText(ImVec2(right + 56.0f, y1), text, plugin.id.c_str());
    dl->AddText(ImVec2(right, y2), muted, "Caps:");
    const std::string capabilities = joinPluginCapabilities(plugin.capabilities);
    dl->AddText(ImVec2(right + 56.0f, y2), text, capabilities.c_str());
    const std::string description = plugin.description.empty() ? plugin.directory.string() : plugin.description;
    dl->AddText(ImVec2(left, start.y + 122.0f), muted, description.c_str());

    float cursorY = start.y + 146.0f;
    if (!plugin.loadError.empty()) {
        dl->AddText(ImVec2(left, cursorY), IM_COL32(224, 92, 92, 255), plugin.loadError.c_str());
        cursorY += 26.0f;
    }
    if (!plugin.settingsSchema.empty()) {
        dl->AddLine(ImVec2(left, cursorY - 4.0f), ImVec2(start.x + width - 14.0f, cursorY - 4.0f), border);
        ImGui::SetCursorScreenPos(ImVec2(left, cursorY));
        ImGui::TextDisabled("%s", tr("Settings"));
        for (const PluginSettingField& field : plugin.settingsSchema) {
            changed |= drawPluginSettingField(context, plugin, field);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height + 8.0f));
    ImGui::PopID();
    return changed;
}

bool drawAboutPage()
{
    ImGui::TextUnformatted("Launcher");
    ImGui::Dummy(ImVec2(1.0f, 8.0f));
    ImGui::TextDisabled("%s", tr("A pure quick launcher"));
    ImGui::TextDisabled("C++20 / Dear ImGui Docking / DirectX 11");
    ImGui::Dummy(ImVec2(1.0f, 12.0f));
    ImGui::TextUnformatted(tr("Name: Launcher"));
    ImGui::TextUnformatted(tr("Runtime: Windows 10+"));
    ImGui::TextUnformatted(tr("Tech stack: C++ / ImGui"));
    return false;
}

bool drawGeneralPage(AppContext& context)
{
    bool changed = false;
    AppSettings& settings = context.persisted().settings;
    ImGui::TextUnformatted(tr("General"));
    section(tr("Language"));
    ImGui::Indent(28.0f);
    ImGui::TextUnformatted(tr("Language"));
    ImGui::SameLine(400.0f);
    ImGui::SetNextItemWidth(220.0f);
    const std::vector<LocaleCatalogEntry> locales = availableLocales();
    std::string languagePreview = tr("Auto");
    if (settings.language != "auto") {
        languagePreview = settings.language;
        for (const LocaleCatalogEntry& entry : locales) {
            if (entry.id == settings.language) {
                languagePreview = entry.name;
                break;
            }
        }
    }
    const UiPalette palette = currentSettingsControlPalette();
    const bool languageComboOpen = beginSettingsCombo(palette, "##language", languagePreview.c_str());
    if (languageComboOpen) {
        ui_anim::pushAppearAlpha(ImGui::GetID("combo-language"), 0.10f, 0.20f);
        if (settingsComboItem(palette, tr("Auto"), settings.language == "auto")) {
            settings.language = "auto";
            setLocale(settings.language);
            changed = true;
        }
        for (const LocaleCatalogEntry& entry : locales) {
            if (settingsComboItem(palette, entry.name.c_str(), settings.language == entry.id)) {
                settings.language = entry.id;
                setLocale(settings.language);
                changed = true;
            }
        }
        ui_anim::popAppearAlpha();
    }
    endSettingsCombo(languageComboOpen);
    ImGui::Unindent(28.0f);

    section(tr("Updates"));
    changed |= rowCheckbox(tr("Automatically check for updates"), &settings.autoCheckUpdates, 28.0f,
                           tr("Check for new Launcher releases automatically."));
    if (!settings.autoCheckUpdates) {
        ImGui::BeginDisabled();
    }
    changed |= rowCheckbox(tr("Check for updates at startup"), &settings.checkUpdatesAtStartup, 28.0f,
                           tr("Check for a new release when Launcher starts."));
    changed |= rowCheckbox(tr("Check for updates daily"), &settings.checkUpdatesDaily, 28.0f,
                           tr("Check for a new release at most once every 24 hours."));
    if (!settings.autoCheckUpdates) {
        ImGui::EndDisabled();
    }

    if (rowCheckbox(tr("Start with Windows"), &settings.startWithWindows)) {
        SystemIntegration::setStartupEnabled(settings.startWithWindows);
        changed = true;
    }
    changed |= rowCheckbox(tr("Hide main window on first start"), &settings.startHidden);
    changed |= rowCheckbox(tr("Fullscreen Do Not Disturb"), &settings.fullscreenDoNotDisturb, 28.0f,
                           tr("Ignore wake hotkey for fullscreen game-like windows; common fullscreen code editors are not blocked."));
    changed |= rowCheckbox(tr("Always on Top"), &settings.alwaysOnTop, 28.0f, tr("Keep the Launcher window group above normal windows."));
    changed |= rowCheckbox(tr("Close button hides to tray"), &settings.minimizeToTray, 28.0f,
                           tr("The close button hides to tray instead of exiting."));
    changed |= rowCheckbox(tr("Taskbar registration"), &settings.showTaskbarIcon, 28.0f,
                           tr("Hide the taskbar button by default and use the tray icon as the entry."));
    changed |= rowCheckbox(tr("Enable global wake hotkey"), &settings.enableGlobalHotkey, 28.0f,
                           tr("Use the hotkey to show or hide Launcher from other apps."));
    ImGui::Indent(28.0f);
    ImGui::TextUnformatted(tr("Global wake hotkey"));
    ImGui::SameLine(400.0f);
    changed |= drawHotkeyInput("##globalHotkey", settings.globalHotkey);
    ImGui::Unindent(28.0f);

    section(tr("Drag and Drop"));
    changed |= rowCheckbox(tr("Enable enhanced drag and drop"), &settings.dragEnhanced, 28.0f,
                           tr("Allow files to be dropped into the main window and added to the current category."));
    changed |= rowCheckbox(tr("Swap when dropped onto a placeholder"), &settings.dragSwapPlaceholder);

    section(tr("Data"));
    changed |= rowCheckbox(tr("Auto update environment variables"), &settings.autoUpdateEnvironment);
    ImGui::Indent(28.0f);
    ImGui::TextUnformatted(tr("Config Directory"));
    ImGui::TextWrapped("%s", configDirectoryText(context).c_str());
    ImGui::TextUnformatted(tr("Config File"));
    ImGui::TextWrapped("%s", configPathText(context).c_str());
    if (ui_anim::button(tr("Open Location"), ImVec2(140.0f, 30.0f))) {
        openConfigLocation(context);
    }
    ImGui::SameLine();
    if (ui_anim::button(tr("Change Location"), ImVec2(140.0f, 30.0f))) {
        changeConfigDirectory(context);
    }
    if (ui_anim::button(tr("Import Config"), ImVec2(140.0f, 30.0f))) {
        importConfig(context);
    }
    ImGui::SameLine();
    if (ui_anim::button(tr("Export Config"), ImVec2(140.0f, 30.0f))) {
        exportConfig(context);
    }
    ImGui::Unindent(28.0f);

    return changed;
}

bool drawActionPage(AppContext& context)
{
    bool changed = false;
    AppSettings& settings = context.persisted().settings;
    ImGui::TextUnformatted(tr("Actions"));
    section(tr("Run"));
    changed |= rowCheckbox(tr("Double-click to run item"), &settings.doubleClickRun);
    changed |= rowCheckbox(tr("Reset scroll after wake"), &settings.keepScrollAfterWake);
    changed |= rowCheckbox(tr("Middle-click category to run all"), &settings.middleClickRunsCategory);
    changed |= rowCheckbox(tr("Run all in virtual folders"), &settings.virtualFolderRunsAll);
    changed |= rowCheckbox(tr("Close button hides"), &settings.minimizeToTray);
    changed |= rowCheckbox(tr("Shift + right-click opens Explorer menu"), &settings.shiftRightClickExplorerMenu);
    changed |= rowCheckbox(tr("Explorer directory search menu"), &settings.directorySearchContextMenu, 28.0f,
                           tr("Add a folder right-click menu that opens search with dir <folder>."));

    section(tr("Window"));
    changed |= rowCheckbox(tr("Drag blank area to move window"), &settings.dragBlankAreaMoveWindow, 28.0f,
                           tr("Allow dragging empty areas of the main window to move it."));

    section(tr("Wake"));
    changed |= rowCheckbox(tr("Enable global hotkey"), &settings.enableGlobalHotkey);
    ImGui::Indent(28.0f);
    ImGui::TextUnformatted(tr("Global hotkey"));
    ImGui::SameLine(400.0f);
    changed |= drawHotkeyInput("##actionGlobalHotkey", settings.globalHotkey);
    ImGui::Unindent(28.0f);
    changed |= rowCheckbox(tr("Fullscreen Do Not Disturb"), &settings.fullscreenDoNotDisturb);
    changed |= rowCheckbox(tr("Snap to screen corners"), &settings.magneticScreenCorner);
    changed |= rowCheckbox(tr("Snap to screen edges"), &settings.magneticScreenEdge);
    changed |= rowCheckbox(tr("Wake on tray hover"), &settings.wakeByTrayHover);

    section(tr("Hide"));
    changed |= rowCheckbox(tr("After mouse leaves main window"), &settings.hideOnMouseLeave);
    changed |= rowCheckbox(tr("After focus is lost"), &settings.hideOnFocusLost);
    changed |= rowCheckbox(tr("Double-click blank area"), &settings.hideOnBlankDoubleClick);
    changed |= rowCheckbox(tr("After running item"), &settings.hideAfterRun);
    changed |= rowCheckbox(tr("Clear selection after main window closes"), &settings.clearSelectionAfterMainClose);
    return changed;
}

bool drawInterfacePage(AppContext& context)
{
    bool changed = false;
    AppSettings& settings = context.persisted().settings;
    ImGui::TextUnformatted(tr("Interface"));
    section(tr("Theme"));
    const UiPalette comboPalette = currentSettingsControlPalette();
    const std::vector<ThemeCatalogEntry>& themes = context.themes.entries();
    const char* preview = context.themes.active().name.empty() ? context.themes.active().id.c_str() : context.themes.active().name.c_str();
    ImGui::Indent(28.0f);
    ImGui::TextUnformatted(tr("Theme"));
    ImGui::SameLine(400.0f);
    ImGui::SetNextItemWidth(220.0f);
    const bool themeComboOpen = beginSettingsCombo(comboPalette, "##theme", preview);
    if (themeComboOpen) {
        ui_anim::pushAppearAlpha(ImGui::GetID("combo-theme"), 0.10f, 0.20f);
        for (const ThemeCatalogEntry& entry : themes) {
            const std::string label = entry.name.empty() ? entry.id : entry.name;
            if (settingsComboItem(comboPalette, label.c_str(), entry.id == context.themes.active().id)) {
                if (context.themes.select(entry.id)) {
                    settings.themeId = context.themes.active().id;
                    changed = true;
                }
            }
        }
        ui_anim::popAppearAlpha();
    }
    endSettingsCombo(themeComboOpen);
    ImGui::Unindent(28.0f);
    ImGui::Indent(28.0f);
    ImGui::TextUnformatted(tr("Theme Editor"));
    ImGui::SameLine(400.0f);
    if (ui_anim::button(tr("Open Theme Editor"), ImVec2(220.0f, 30.0f))) {
        context.runtime().showThemeEditor = true;
    }
    ImGui::Unindent(28.0f);

    section(tr("Animation"));
    changed |= rowCheckbox(tr("Enable animations"), &settings.enableAnimations, 28.0f,
                           tr("Animate checkbox, button ripple, menus, item layout, drag ghost, and reorder transitions."));
    changed |= rowSliderInt(tr("Animation speed"), &settings.animationSpeedPercent, 25, 250, "%d%%");

    section(tr("Window"));
    changed |= rowCheckbox(tr("Reset Position"), &settings.enableViewport);
    changed |= rowCheckbox(tr("Lock Position"), &settings.lockWindowPosition);
    changed |= rowCheckbox(tr("Lock Size"), &settings.lockWindowSize);
    changed |= rowCheckbox(tr("Lock Layout"), &settings.lockItemLayout);
    changed |= rowCheckbox(tr("Always on Top"), &settings.alwaysOnTop);

    section(tr("Global Default Layout"));
    int viewMode = static_cast<int>(settings.viewMode);
    if (rowCombo(tr("Item view type"), &viewMode, tr("Item View Type Items"))) {
        settings.viewMode = static_cast<ItemViewMode>(viewMode);
        changed = true;
    }
    changed |= rowSliderInt(tr("Item icon size"), &settings.iconSize, 24, 96);
    changed |= rowSliderInt(tr("Item name lines"), &settings.nameLines, 0, 3);

    section(tr("Title bar buttons"));
    changed |= rowCheckbox(tr("Search"), &settings.showSearchButton);
    changed |= rowCheckbox(tr("Menu"), &settings.showMenuButton);
    changed |= rowCheckbox(tr("Close"), &settings.showCloseButton);

    section(tr("Tooltip"));
    changed |= rowCheckbox(tr("Enable"), &settings.tooltipEnabled, 28.0f,
                           tr("Show run count, target, arguments, remarks, and other details when hovering an item."));
    changed |= rowSliderInt(tr("Item tooltip opacity"), &settings.itemTooltipOpacity, 0, 100, "%d%%");
    changed |= rowCheckbox(tr("Run count"), &settings.tooltipRunCount, 56.0f);
    changed |= rowCheckbox(tr("Target"), &settings.tooltipTarget, 56.0f);
    changed |= rowCheckbox(tr("Arguments"), &settings.tooltipArguments, 56.0f);
    changed |= rowCheckbox(tr("Remark"), &settings.tooltipRemark, 56.0f);
    changed |= rowCheckbox(tr("Created at"), &settings.tooltipCreatedAt, 56.0f);
    changed |= rowCheckbox(tr("Last edited at"), &settings.tooltipLastEditedAt, 56.0f);
    changed |= rowCheckbox(tr("Last run at"), &settings.tooltipLastRunAt, 56.0f);
    return changed;
}

bool drawSearchPage(AppContext& context)
{
    bool changed = false;
    AppSettings& settings = context.persisted().settings;
    ImGui::TextUnformatted(tr("Search"));
    section(tr("General"));
    changed |= rowCheckbox(tr("Smooth input"), &settings.smoothInput);
    changed |= rowSliderInt(tr("Search delay"), &settings.searchDelayMs, 0, 1000, "%d ms");
    changed |= rowCheckbox(tr("Enable Global Search"), &settings.enableGlobalSearch, 28.0f,
                           tr("Index local fixed and removable drives in the background."));
    changed |=
        rowCombo(tr("Global search scan intensity"), &settings.globalSearchScanIntensity, tr("Global Search Scan Intensity Items"), 220.0f,
                 tr("Controls how often background roots are refreshed and skips noisy cache/build/system folders during scans."));
    settings.globalSearchScanIntensity = std::clamp(settings.globalSearchScanIntensity, 0, 2);
    ImGui::Indent(28.0f);
    const GlobalIndexProgress progress = context.globalFiles.progress();
    const bool indexing = context.globalFiles.indexing() || progress.active;
    if (!settings.enableGlobalSearch) {
        ImGui::BeginDisabled();
    }
    const char* rebuildLabel = indexing ? tr("Cancel Global Search Rebuild") : tr("Rebuild Global Search Cache");
    if (ui_anim::button(rebuildLabel, ImVec2(280.0f, 30.0f))) {
        if (indexing) {
            context.cancelGlobalSearchRebuild();
        } else {
            context.rebuildGlobalSearch();
        }
    }
    helpMarker(tr("Rescan and replace the global file index root by root. Click again while rebuilding to cancel. After cancel, existing "
                  "caches stay in use for about 30 minutes."));
    if (progress.active || indexing) {
        float fraction = 0.0f;
        if (progress.totalRoots > 0) {
            const float rootBase = static_cast<float>(progress.completedRoots) / static_cast<float>(progress.totalRoots);
            const float rootSpan = 1.0f / static_cast<float>(progress.totalRoots);
            fraction = rootBase + rootSpan * std::clamp(progress.currentRootFraction, 0.0f, 1.0f);
        } else {
            fraction = std::clamp(progress.currentRootFraction, 0.0f, 1.0f);
        }
        char overlay[64]{};
        std::snprintf(overlay, sizeof(overlay), "%.0f%%", std::clamp(fraction, 0.0f, 1.0f) * 100.0f);
        ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(320.0f, 0.0f), overlay);

        std::string status = tr("Global search index progress");
        status += ": ";
        status += std::to_string(progress.completedRoots);
        status += "/";
        status += std::to_string(progress.totalRoots);
        status += " ";
        status += tr("roots");
        status += " | ";
        status += std::to_string(progress.indexedFiles);
        status += " ";
        status += tr("files");
        if (progress.currentRootFiles > 0) {
            status += " | ";
            status += tr("current root");
            status += " ";
            status += std::to_string(progress.currentRootFiles);
        }
        if (!progress.currentRoot.empty()) {
            status += " | ";
            status += progress.currentRoot;
        }
        ImGui::TextWrapped("%s", status.c_str());
        if (!progress.currentPath.empty()) {
            ImGui::TextWrapped("%s", progress.currentPath.c_str());
        }
    }
    if (!settings.enableGlobalSearch) {
        ImGui::EndDisabled();
    }
    ImGui::Unindent(28.0f);
    changed |= rowSliderInt(tr("Search results per page"), &settings.searchResultLimit, 20, 512);
    changed |= rowSliderInt(tr("Global files per page"), &settings.globalSearchResultLimit, 0, 512);
    changed |= rowCheckbox(tr("Hide system search results"), &settings.globalSearchHideSystemPaths, 28.0f,
                           tr("Exclude noisy system, cache, package, and temporary paths from global file results."));
    changed |= rowCheckbox(tr("Use default icons"), &settings.useDefaultIcons, 28.0f,
                           tr("Skip loading file icons and use lightweight default icons to reduce memory use."));
    changed |= rowCheckbox(tr("Enable Advanced Search"), &settings.advancedSearch, 28.0f,
                           tr("Use weighted fuzzy matching and usage-aware ranking for launcher items."));
    changed |= rowCheckbox(tr("Enable search hotkey"), &settings.enableSearchHotkey, 28.0f,
                           tr("Show Launcher and focus the search box directly."));
    ImGui::Indent(28.0f);
    ImGui::TextUnformatted(tr("Search hotkey"));
    ImGui::SameLine(400.0f);
    changed |= drawHotkeyInput("##searchHotkey", settings.searchHotkey);
    ImGui::Unindent(28.0f);

    section(tr("Actions"));
    changed |= rowCheckbox(tr("Hide search page after main window closes"), &settings.hideSearchAfterMainClose);
    changed |= rowCheckbox(tr("Hide main window after running item"), &settings.runItemHidesMain);
    changed |= rowCheckbox(tr("Close search page after running item"), &settings.closeSearchAfterRun);
    changed |= rowCheckbox(tr("Alt + number runs search result"), &settings.searchAltNumberRun);

    section(tr("Match Mode"));
    changed |= rowCheckbox(tr("Regular expression"), &settings.searchRegex, 28.0f, tr("Treat search text as a regular expression."));
    changed |= rowCheckbox(tr("Pinyin initials"), &settings.searchPinyinInitial);
    changed |= rowCheckbox(tr("Pinyin"), &settings.searchPinyin);
    changed |= rowCheckbox(tr("Greedy mode"), &settings.searchEnglishMode);

    section(tr("Search Scope"));
    changed |= rowCheckbox(tr("Target"), &settings.searchScopeTarget);
    changed |= rowCheckbox(tr("Remark"), &settings.searchScopeRemark);

    section(tr("Boost"));
    changed |= rowCheckbox(tr("Parameter variable - search"), &settings.searchParamVariable, 28.0f,
                           tr("Replace %so% and %so-url% with the current search parameter when running search results."));
    return changed;
}

bool drawPluginPage(AppContext& context)
{
    bool changed = false;
    const float headerY = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(headerY + (30.0f - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::TextUnformatted(tr("Installed Extensions"));
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::SetCursorPosY(headerY);
    if (ui_anim::button(tr("Reload"), ImVec2(96.0f, 30.0f))) {
        context.reloadPlugins();
        changed = true;
    }
    ImGui::SetCursorPosY(headerY + 30.0f);
    ImGui::Dummy(ImVec2(1.0f, 12.0f));

    const std::vector<PluginInfo>& plugins = context.plugins.plugins();
    if (plugins.empty()) {
        ImGui::TextDisabled("%s", tr("No plugins installed"));
        ImGui::Dummy(ImVec2(1.0f, 6.0f));
        ImGui::TextDisabled("%s", tr("Plugin directories:"));
        for (const std::filesystem::path& root : context.pluginRoots()) {
            ImGui::BulletText("%s", root.string().c_str());
        }
        return changed;
    }

    for (const PluginInfo& plugin : plugins) {
        changed |= drawPluginCard(plugin, context);
    }
    return changed;
}

bool drawHelpPage()
{
    drawUserGuideInline(currentSettingsControlPalette());
    return false;
}

} // namespace

void requestSettingsTab(AppContext& context, int tab)
{
    context.runtime().showSettings = true;
    gPendingSettingsTab = std::clamp(tab, 0, 6);
    gSettingsNeedsFocus = true;
}

void drawSettingsPanel(AppContext& context)
{
    if (!context.runtime().showSettings) {
        if (gSettingsWasVisible) {
            clearSettingsSearch();
            gSettingsTrimFramesRemaining = 4;
        }
        if (gSettingsTrimFramesRemaining > 0) {
            trimSettingsWorkingSet();
            --gSettingsTrimFramesRemaining;
        }
        gSettingsWasVisible = false;
        gSettingsLayerKnown = false;
        return;
    }

    if (!gSettingsWasVisible) {
        gSettingsNeedsFocus = true;
        gSettingsWasVisible = true;
        gSettingsTrimFramesRemaining = 0;
    }

    static int activeTab = 0;
    if (gPendingSettingsTab >= 0) {
        activeTab = gPendingSettingsTab;
        gPendingSettingsTab = -1;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiWindowClass settingsClass{};
    settingsClass.ClassId = ImGui::GetID("LauncherSettingsViewport");
    settingsClass.ViewportFlagsOverrideSet =
        ImGuiViewportFlags_NoTaskBarIcon | ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&settingsClass);
    const ImVec2 settingsSize(std::min(800.0f, std::max(620.0f, viewport->WorkSize.x - 40.0f)),
                              std::min(650.0f, std::max(520.0f, viewport->WorkSize.y - 40.0f)));
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(settingsSize, ImGuiCond_Appearing);
    if (gSettingsNeedsFocus) {
        ImGui::SetNextWindowFocus();
    }
    applySettingsStyle(context);
    bool changed = false;
    bool settingsOpen = true;
    if (ImGui::Begin("Settings", &settingsOpen,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoSavedSettings)) {
        if (auto* hwnd = static_cast<HWND>(ImGui::GetWindowViewport()->PlatformHandleRaw)) {
            applyManagedViewportChrome(hwnd, context.themes.active(), uiPalette(context.themes.active()));
            if (!gSettingsLayerKnown || gSettingsHwnd != hwnd) {
                gSettingsHwnd = hwnd;
                gSettingsTopmost = context.persisted().settings.alwaysOnTop;
                gSettingsLayerKnown = true;
            }
            if (gSettingsNeedsFocus) {
                ShowWindow(hwnd, SW_SHOWNORMAL);
                BringWindowToTop(hwnd);
                SetForegroundWindow(hwnd);
                SetFocus(hwnd);
                gSettingsNeedsFocus = false;
            }
        }
        {
            if (drawSettingsTitleBar(uiPalette(context.themes.active()))) {
                context.runtime().showSettings = false;
                clearSettingsSearch();
                ImGui::End();
                popSettingsStyle();
                return;
            }
        }
        ImGui::SetCursorPos(ImVec2(0.0f, kUiTitleHeight));
        drawSidebar(context, activeTab);

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, uiPalette(context.themes.active()).childBg);
        const ImGuiWindowFlags contentFlags = activeTab == 5 ? ImGuiWindowFlags_HorizontalScrollbar : ImGuiWindowFlags_None;
        ImGui::BeginChild("settings-content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, contentFlags);
        ImGui::Dummy(ImVec2(1.0f, 16.0f));
        ImGui::Indent(32.0f);
        if (gSettingsSearchOpen) {
            ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x - 32.0f));
            if (gSettingsSearchNeedsFocus) {
                ImGui::SetKeyboardFocusHere();
                gSettingsSearchNeedsFocus = false;
            }
            ImGui::InputTextWithHint("##settings-search", tr("Search settings"), &gSettingsSearch);
            ImGui::Dummy(ImVec2(1.0f, 12.0f));
        }
        if (!gSettingsSearch.empty()) {
            changed |= drawSettingsSearchResults(activeTab);
        } else {
            switch (activeTab) {
            case 0: changed |= drawGeneralPage(context); break;
            case 1: changed |= drawActionPage(context); break;
            case 2: changed |= drawInterfacePage(context); break;
            case 3: changed |= drawSearchPage(context); break;
            case 4: changed |= drawPluginPage(context); break;
            case 5: changed |= drawHelpPage(); break;
            case 6: changed |= drawAboutPage(); break;
            default: break;
            }
        }
        ImGui::Unindent(32.0f);
        ImGui::Dummy(ImVec2(1.0f, 56.0f));
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::End();
    if (!settingsOpen) {
        context.runtime().showSettings = false;
        clearSettingsSearch();
    }
    popSettingsStyle();

    if (changed) {
        context.save();
    }
}

} // namespace launcher

#include "ui/dock/MainDockMenu.hpp"

#include "app/AppContext.hpp"
#include "ui/common/Localization.hpp"
#include "ui/dock/MainDockChrome.hpp"
#include "ui/dock/MainDockState.hpp"
#include "ui/common/UiAnimation.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace launcher {
namespace {

constexpr float kMenuRowRounding = 5.0f;
bool gMenuShortcutHintsVisible = true;

Category* selectedCategory(AppContext& context)
{
    PersistedState& persisted = context.persisted();
    const RuntimeState& runtime = context.runtime();
    if (runtime.selectedCategory < 0 || runtime.selectedCategory >= static_cast<int>(persisted.categories.size())) {
        return nullptr;
    }
    return &persisted.categories[runtime.selectedCategory];
}

void ensureLocalLayout(Category& category, const AppSettings& settings)
{
    if (!category.useGlobalLayout) {
        return;
    }
    category.useGlobalLayout = false;
    category.viewMode = settings.viewMode;
    category.iconSize = settings.iconSize;
    category.nameLines = settings.nameLines;
}

ImU32 menuHoverColor(const UiPalette& theme, bool open)
{
    ImVec4 color = open ? theme.headerActive : theme.headerHovered;
    color.w = std::max(color.w, open ? 0.70f : 0.62f);
    return ImGui::ColorConvertFloat4ToU32(color);
}

void drawMenuRowBackground(ImDrawList* drawList, const UiPalette& theme, bool active, bool open)
{
    if (!active) {
        return;
    }
    drawList->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), menuHoverColor(theme, open), kMenuRowRounding);
}

} // namespace

void setMenuShortcutHintsVisible(bool visible)
{
    gMenuShortcutHintsVisible = visible;
}

bool menuItem(const UiPalette& theme, const char* icon, const char* label, const char* shortcut, bool selected, bool enabled)
{
    const std::string fullLabel = icon && icon[0] ? std::string(icon) + "  " + label : std::string(label);
    const char* visibleShortcut = gMenuShortcutHintsVisible ? shortcut : nullptr;
    const float shortcutY = ImGui::GetCursorScreenPos().y;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSplit(2);
    drawList->ChannelsSetCurrent(1);
    if (visibleShortcut) {
        ImVec4 hiddenShortcut = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        hiddenShortcut.w = 0.0f;
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, hiddenShortcut);
    }
    const bool clicked = ImGui::MenuItem(fullLabel.c_str(), visibleShortcut, selected, enabled);
    if (visibleShortcut) {
        ImGui::PopStyleColor();
        const ImVec2 shortcutSize = ImGui::CalcTextSize(visibleShortcut);
        const float shortcutX = ImGui::GetItemRectMax().x - shortcutSize.x;
        ImVec4 shortcutColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        if (!enabled) {
            shortcutColor.w *= ImGui::GetStyle().DisabledAlpha;
        }
        drawList->AddText(ImVec2(shortcutX, shortcutY), ImGui::ColorConvertFloat4ToU32(shortcutColor), visibleShortcut);
    }
    const bool hovered = enabled && ImGui::IsItemHovered();
    drawList->ChannelsSetCurrent(0);
    drawMenuRowBackground(drawList, theme, hovered, false);
    drawList->ChannelsMerge();
    return clicked;
}

bool menuToggleItem(const UiPalette& theme, const char* icon, const char* label, bool selected, bool enabled)
{
    const std::string fullLabel = icon && icon[0] ? std::string(icon) + "  " + label : std::string(label);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSplit(2);
    drawList->ChannelsSetCurrent(1);
    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
    const bool clicked = ImGui::MenuItem(fullLabel.c_str(), nullptr, selected, enabled);
    ImGui::PopItemFlag();
    const bool hovered = enabled && ImGui::IsItemHovered();
    drawList->ChannelsSetCurrent(0);
    drawMenuRowBackground(drawList, theme, hovered, false);
    drawList->ChannelsMerge();
    return clicked;
}

bool beginIconMenu(const UiPalette& theme, const char* icon, const char* label)
{
    const std::string fullLabel = icon && icon[0] ? std::string(icon) + "  " + label : std::string(label);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSplit(2);
    drawList->ChannelsSetCurrent(1);
    const bool open = ImGui::BeginMenu(fullLabel.c_str());
    const bool hovered = ImGui::IsItemHovered();
    drawList->ChannelsSetCurrent(0);
    drawMenuRowBackground(drawList, theme, hovered || open, open);
    drawList->ChannelsMerge();
    if (open) {
        suppressCurrentViewportNativeBorder();
        ui_anim::pushAppearAlpha(ImGui::GetID((std::string("menu-open-") + label).c_str()), 0.10f, 0.24f);
    }
    return open;
}

void endIconMenu()
{
    ui_anim::popAppearAlpha();
    ImGui::EndMenu();
}

void drawViewMenu(const UiPalette& theme, AppContext& context)
{
    AppSettings& settings = context.persisted().settings;
    Category* category = selectedCategory(context);
    ItemViewMode viewMode = category && !category->useGlobalLayout ? category->viewMode : settings.viewMode;
    int iconSize = category && !category->useGlobalLayout ? category->iconSize : settings.iconSize;
    int nameLines = category && !category->useGlobalLayout ? category->nameLines : settings.nameLines;
    const bool listLocked = currentListLayoutLocked(context);
    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 3.0f));
    if (menuToggleItem(theme, "", tr("Lock Layout"), listLocked)) {
        setCurrentListLayoutLocked(context, !listLocked);
        changed = true;
    }
    ImGui::Separator();
    if (category && menuToggleItem(theme, "", tr("Use global settings"), category->useGlobalLayout)) {
        category->useGlobalLayout = !category->useGlobalLayout;
        changed = true;
    }
    auto setViewMode = [&](ItemViewMode mode) {
        if (category) {
            ensureLocalLayout(*category, settings);
            category->viewMode = mode;
        } else {
            settings.viewMode = mode;
        }
        changed = true;
    };
    auto setIconSize = [&](int size) {
        if (category) {
            ensureLocalLayout(*category, settings);
            category->iconSize = size;
        } else {
            settings.iconSize = size;
        }
        changed = true;
    };
    auto setNameLines = [&](int lines) {
        if (category) {
            ensureLocalLayout(*category, settings);
            category->nameLines = lines;
        } else {
            settings.nameLines = lines;
        }
        changed = true;
    };
    if (menuToggleItem(theme, "", tr("Icon"), viewMode == ItemViewMode::Icon)) {
        setViewMode(ItemViewMode::Icon);
    }
    if (menuToggleItem(theme, "", tr("List"), viewMode == ItemViewMode::List)) {
        setViewMode(ItemViewMode::List);
    }
    if (menuToggleItem(theme, "", tr("Tile"), viewMode == ItemViewMode::Tile)) {
        setViewMode(ItemViewMode::Tile);
    }
    ImGui::Separator();
    if (menuToggleItem(theme, "", tr("Small icon"), iconSize == 36)) {
        setIconSize(36);
    }
    if (menuToggleItem(theme, "", tr("Medium icon"), iconSize == 48)) {
        setIconSize(48);
    }
    if (menuToggleItem(theme, "", tr("Large icon"), iconSize == 60)) {
        setIconSize(60);
    }
    ImGui::Separator();
    if (menuToggleItem(theme, "", tr("Hide name"), nameLines == 0)) {
        setNameLines(0);
    }
    if (menuToggleItem(theme, "", tr("One line"), nameLines == 1)) {
        setNameLines(1);
    }
    if (menuToggleItem(theme, "", tr("Two lines"), nameLines == 2)) {
        setNameLines(2);
    }
    if (changed) {
        context.save();
    }
    ImGui::PopStyleVar();
}

void drawSortMenu(const UiPalette& theme, AppContext& context)
{
    AppSettings& settings = context.persisted().settings;
    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 3.0f));
    if (menuToggleItem(theme, "", tr("Free"), settings.sortMode == SortMode::Free)) {
        settings.sortMode = SortMode::Free;
        changed = true;
    }
    if (menuToggleItem(theme, "", tr("Name"), settings.sortMode == SortMode::Name)) {
        settings.sortMode = SortMode::Name;
        changed = true;
    }
    if (menuToggleItem(theme, "", tr("Type"), settings.sortMode == SortMode::Type)) {
        settings.sortMode = SortMode::Type;
        changed = true;
    }
    if (menuToggleItem(theme, "", tr("Run count"), settings.sortMode == SortMode::RunCount)) {
        settings.sortMode = SortMode::RunCount;
        changed = true;
    }
    if (menuToggleItem(theme, "", tr("Created at"), settings.sortMode == SortMode::CreatedAt)) {
        settings.sortMode = SortMode::CreatedAt;
        changed = true;
    }
    if (menuToggleItem(theme, "", tr("Last run at"), settings.sortMode == SortMode::LastRunAt)) {
        settings.sortMode = SortMode::LastRunAt;
        changed = true;
    }
    if (menuToggleItem(theme, "", tr("Last edited at"), settings.sortMode == SortMode::LastEditedAt)) {
        settings.sortMode = SortMode::LastEditedAt;
        changed = true;
    }
    if (changed) {
        context.save();
    }
    ImGui::PopStyleVar();
}

} // namespace launcher

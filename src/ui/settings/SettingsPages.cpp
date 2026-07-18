#include "ui/settings/SettingsPages.hpp"

#include "app/AppContext.hpp"
#include "ui/common/Localization.hpp"
#include "ui/settings/SettingsWidgets.hpp"
#include "ui/common/UiAnimation.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace launcher {

bool drawActionSettingsPage(AppContext& context)
{
    using namespace settings_ui;
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

bool drawInterfaceSettingsPage(AppContext& context)
{
    using namespace settings_ui;
    bool changed = false;
    AppSettings& settings = context.persisted().settings;
    ImGui::TextUnformatted(tr("Interface"));
    section(tr("Theme"));
    const UiPalette comboPalette = currentControlPalette();
    const std::vector<ThemeCatalogEntry>& themes = context.themes.entries();
    const char* preview = context.themes.active().name.empty() ? context.themes.active().id.c_str() : context.themes.active().name.c_str();
    ImGui::Indent(28.0f);
    ImGui::TextUnformatted(tr("Theme"));
    ImGui::SameLine(400.0f);
    ImGui::SetNextItemWidth(220.0f);
    const bool themeComboOpen = beginCombo(comboPalette, "##theme", preview);
    if (themeComboOpen) {
        ui_anim::pushAppearAlpha(ImGui::GetID("combo-theme"), 0.10f, 0.20f);
        for (const ThemeCatalogEntry& entry : themes) {
            const std::string label = entry.name.empty() ? entry.id : entry.name;
            if (comboItem(comboPalette, label.c_str(), entry.id == context.themes.active().id) && context.themes.select(entry.id)) {
                settings.themeId = context.themes.active().id;
                changed = true;
            }
        }
        ui_anim::popAppearAlpha();
    }
    endCombo(themeComboOpen);
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

    section(tr("Menu"));
    changed |= rowCheckbox(tr("Show menu shortcut hints"), &settings.showMenuShortcutHints, 28.0f,
                           tr("Display keyboard shortcuts on context menu items."));

    section(tr("Tooltip"));
    changed |= rowCheckbox(tr("Enable"), &settings.tooltipEnabled, 28.0f,
                           tr("Show run count, target, arguments, remarks, and other details when hovering an item."));
    changed |= rowCheckbox(tr("Follow mouse when shown"), &settings.tooltipFollowMouse, 28.0f,
                           tr("Show the item tooltip next to the mouse cursor instead of below the item."));
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

bool drawSearchSettingsPage(AppContext& context)
{
    using namespace settings_ui;
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
    if (!settings.enableGlobalSearch) ImGui::BeginDisabled();
    const char* rebuildLabel = indexing ? tr("Cancel Global Search Rebuild") : tr("Rebuild Global Search Cache");
    if (ui_anim::button(rebuildLabel, ImVec2(280.0f, 30.0f))) {
        indexing ? context.cancelGlobalSearchRebuild() : context.rebuildGlobalSearch();
    }
    helpMarker(tr("Rescan and replace the global file index root by root. Click again while rebuilding to cancel. After cancel, existing "
                  "caches stay in use for about 30 minutes."));
    if (progress.active || indexing) {
        float fraction = progress.totalRoots > 0
                             ? static_cast<float>(progress.completedRoots) / static_cast<float>(progress.totalRoots) +
                                   (1.0f / static_cast<float>(progress.totalRoots)) * std::clamp(progress.currentRootFraction, 0.0f, 1.0f)
                             : std::clamp(progress.currentRootFraction, 0.0f, 1.0f);
        fraction = std::clamp(fraction, 0.0f, 1.0f);
        char overlay[64]{};
        std::snprintf(overlay, sizeof(overlay), "%.0f%%", fraction * 100.0f);
        ImGui::ProgressBar(fraction, ImVec2(320.0f, 0.0f), overlay);

        std::string status = std::string(tr("Global search index progress")) + ": " + std::to_string(progress.completedRoots) + "/" +
                             std::to_string(progress.totalRoots) + " " + tr("roots") + " | " + std::to_string(progress.indexedFiles) + " " +
                             tr("files");
        if (progress.currentRootFiles > 0) {
            status += " | " + std::string(tr("current root")) + " " + std::to_string(progress.currentRootFiles);
        }
        if (!progress.currentRoot.empty()) status += " | " + progress.currentRoot;
        ImGui::TextWrapped("%s", status.c_str());
        if (!progress.currentPath.empty()) ImGui::TextWrapped("%s", progress.currentPath.c_str());
    }
    if (!settings.enableGlobalSearch) ImGui::EndDisabled();
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

} // namespace launcher

#include "ui/MainDockContextMenus.hpp"

#include "app/AppContext.hpp"
#include "ui/Localization.hpp"
#include "ui/MainDockChrome.hpp"
#include "ui/MainDockMenu.hpp"
#include "ui/MaterialIcons.hpp"
#include "ui/UiAnimation.hpp"

#include <windows.h>
#include <imgui.h>

#include <algorithm>

namespace launcher {

void drawContentMenu(const UiPalette& theme, AppContext& context, const ContentMenuState& state, const ContentMenuApi& api)
{
    const AppSettings& settings = context.persisted().settings;
    const int popupOpacity = context.themes.active().popupMenuOpacity;
    const UiPalette popupTheme = withPopupOpacity(theme, popupOpacity);
    LightPopupStyle popupStyle(popupTheme, popupOpacity);
    const bool animatedPopup = ui_anim::pushPopupAppear("content-menu");
    if (ImGui::BeginPopup("content-menu")) {
        suppressCurrentViewportNativeBorder();
        if (beginIconMenu(popupTheme, Icons::Layout, tr("View"))) {
            if (api.drawViewMenu) {
                api.drawViewMenu(popupTheme, context);
            }
            endIconMenu();
        }
        if (beginIconMenu(popupTheme, Icons::Sort, tr("Sort By"))) {
            if (api.drawSortMenu) {
                api.drawSortMenu(popupTheme, context);
            }
            endIconMenu();
        }
        if (menuItem(popupTheme, Icons::Refresh, tr("Refresh"), "F5")) {
            context.rebuildSearch();
        }
        ImGui::Separator();
        if (menuItem(popupTheme, Icons::Paste, tr("Paste"), "Ctrl+V", false, state.clipboardAvailable) && api.pasteClipboardItem) {
            api.pasteClipboardItem(context);
        }
        if (beginIconMenu(popupTheme, Icons::Add, tr("New"))) {
            if (menuItem(popupTheme, Icons::Title, tr("Title")) && api.openItemEditor)
                api.openItemEditor(context, -1, LaunchItemType::Title);
            if (beginIconMenu(popupTheme, Icons::Placeholder, tr("Placeholder"))) {
                if (menuItem(popupTheme, "", "1") && api.appendPlaceholders) api.appendPlaceholders(context, 1);
                if (menuItem(popupTheme, "", "2") && api.appendPlaceholders) api.appendPlaceholders(context, 2);
                if (menuItem(popupTheme, "", "3") && api.appendPlaceholders) api.appendPlaceholders(context, 3);
                endIconMenu();
            }
            if (menuItem(popupTheme, Icons::Folder, tr("Virtual Folder")) && api.openItemEditor)
                api.openItemEditor(context, -1, LaunchItemType::VirtualFolder);
            if (menuItem(popupTheme, Icons::Note, tr("New Note")) && api.appendNoteItem) api.appendNoteItem(context);
            ImGui::Separator();
            if (menuItem(popupTheme, Icons::Script, "Script") && api.openItemEditor)
                api.openItemEditor(context, -1, LaunchItemType::Script);
            if (menuItem(popupTheme, Icons::Link, "Url") && api.openItemEditor) api.openItemEditor(context, -1, LaunchItemType::Url);
            if (menuItem(popupTheme, Icons::EmptyItem, tr("Empty Item"), "Ctrl+N") && api.openItemEditor)
                api.openItemEditor(context, -1, LaunchItemType::App);
            ImGui::Separator();
            if (beginIconMenu(popupTheme, Icons::Layout, tr("Built-in Items"))) {
                if (menuItem(popupTheme, "", tr("Calculator")) && api.appendBuiltInItem)
                    api.appendBuiltInItem(context, "Calculator", "calc.exe", {});
                if (menuItem(popupTheme, "", tr("Command Prompt")) && api.appendBuiltInItem)
                    api.appendBuiltInItem(context, "Cmd", "cmd.exe", {});
                if (menuItem(popupTheme, "", tr("Task Manager")) && api.appendBuiltInItem)
                    api.appendBuiltInItem(context, "Task Manager", "taskmgr.exe", {});
                if (menuItem(popupTheme, "", tr("Environment")) && api.appendBuiltInItem)
                    api.appendBuiltInItem(context, "Environment", "rundll32.exe", "sysdm.cpl,EditEnvironmentVariables");
                endIconMenu();
            }
            endIconMenu();
        }
        ImGui::Separator();
        if (menuItem(popupTheme, Icons::RunAll, tr("Run All"))) {
            if (api.currentItems && api.runItemsInList) {
                if (std::vector<LaunchItem>* items = api.currentItems(context)) {
                    api.runItemsInList(context, *items);
                    if (settings.hideAfterRun && api.requestHideMainWindow) {
                        api.requestHideMainWindow();
                    }
                }
            }
        }
        if (menuItem(popupTheme, Icons::Download, tr("Download All Link Icons")) && api.clearAllLinkIcons) {
            api.clearAllLinkIcons();
        }
        ImGui::Separator();
        if (menuItem(popupTheme, Icons::Delete, tr("Delete All Items"), "Ctrl+Shift+Del")) {
            if (api.currentItems && api.requestDeleteIds) {
                if (std::vector<LaunchItem>* items = api.currentItems(context)) {
                    std::vector<std::string> ids;
                    ids.reserve(items->size());
                    for (const LaunchItem& item : *items) {
                        ids.push_back(item.id);
                    }
                    api.requestDeleteIds(context, std::move(ids));
                }
            }
        }
        ImGui::EndPopup();
    }
    if (animatedPopup) {
        ui_anim::popAppearAlpha();
    }
}

void drawItemMenu(const UiPalette& theme, AppContext& context, std::vector<LaunchItem>& items, int itemIndex, const ItemMenuApi& api)
{
    if (itemIndex < 0 || itemIndex >= static_cast<int>(items.size())) {
        return;
    }
    LaunchItem& item = items[itemIndex];
    const AppSettings& settings = context.persisted().settings;
    const int popupOpacity = context.themes.active().popupMenuOpacity;
    const UiPalette popupTheme = withPopupOpacity(theme, popupOpacity);
    LightPopupStyle popupStyle(popupTheme, popupOpacity);
    const bool animatedPopup = ui_anim::pushPopupAppear("item-menu");
    if (ImGui::BeginPopup("item-menu")) {
        suppressCurrentViewportNativeBorder();
        if (api.isItemSelected && !api.isItemSelected(item)) {
            if (api.selectSingle) {
                api.selectSingle(context, item);
            }
        } else {
            context.runtime().selectedItemId = item.id;
        }

        if (item.type == LaunchItemType::Title) {
            if (menuItem(popupTheme, Icons::Edit, tr("Edit"), "F2") && api.openItemEditor) api.openItemEditor(context, itemIndex);
            if (menuItem(popupTheme, Icons::Delete, tr("Delete"), "Del") && api.requestDeleteSelection) {
                api.requestDeleteSelection(context);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            if (animatedPopup) {
                ui_anim::popAppearAlpha();
            }
            return;
        }

        if (item.type == LaunchItemType::VirtualFolder) {
            if (menuItem(popupTheme, Icons::Folder, tr("Open"), "Enter") && api.enterVirtualFolder) {
                api.enterVirtualFolder(context, item);
                ImGui::CloseCurrentPopup();
            }
            if (menuItem(popupTheme, Icons::Task, tr("Add to Task Planner")) && api.addScheduledTask) {
                api.addScheduledTask(context, item);
                ImGui::CloseCurrentPopup();
            }
            if (beginIconMenu(popupTheme, Icons::CopyProperties, tr("Copy Properties"))) {
                if (menuItem(popupTheme, "", tr("All")) && api.itemPropertiesText && api.copyTextToClipboard)
                    api.copyTextToClipboard(api.itemPropertiesText(item));
                if (menuItem(popupTheme, "", tr("Name")) && api.copyTextToClipboard) api.copyTextToClipboard(item.name);
                if (menuItem(popupTheme, "", tr("Remark")) && api.copyTextToClipboard) api.copyTextToClipboard(item.remark);
                endIconMenu();
            }
            ImGui::Separator();
            if (menuItem(popupTheme, Icons::Copy, tr("Copy"), "Ctrl+C") && api.copyItemToClipboard) api.copyItemToClipboard(item, false);
            if (menuItem(popupTheme, Icons::Cut, tr("Cut"), "Ctrl+X") && api.copyItemToClipboard) api.copyItemToClipboard(item, true);
            ImGui::Separator();
            if (menuItem(popupTheme, Icons::Edit, tr("Edit"), "F2") && api.openItemEditor) api.openItemEditor(context, itemIndex);
            if (menuItem(popupTheme, Icons::Delete, tr("Delete"), "Del") && api.requestDeleteSelection) {
                api.requestDeleteSelection(context);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            if (animatedPopup) {
                ui_anim::popAppearAlpha();
            }
            return;
        }

        if (item.type == LaunchItemType::Note) {
            if (menuItem(popupTheme, Icons::Note, tr("Open"), "Enter") && api.runItem) {
                api.runItem(context, item, SW_SHOWNORMAL);
                ImGui::CloseCurrentPopup();
            }
            if (menuItem(popupTheme, Icons::Edit, tr("Edit Content"), "Ctrl+E") && api.openNoteEditor) {
                api.openNoteEditor(context, item);
                ImGui::CloseCurrentPopup();
            }
            if (beginIconMenu(popupTheme, Icons::CopyProperties, tr("Copy Properties"))) {
                if (menuItem(popupTheme, "", tr("All")) && api.itemPropertiesText && api.copyTextToClipboard)
                    api.copyTextToClipboard(api.itemPropertiesText(item));
                if (menuItem(popupTheme, "", tr("Name")) && api.copyTextToClipboard) api.copyTextToClipboard(item.name);
                if (menuItem(popupTheme, "", tr("Target")) && api.copyTextToClipboard) api.copyTextToClipboard(item.target.string());
                if (menuItem(popupTheme, "", tr("Remark")) && api.copyTextToClipboard) api.copyTextToClipboard(item.remark);
                endIconMenu();
            }
            ImGui::Separator();
            if (menuItem(popupTheme, Icons::Copy, tr("Copy"), "Ctrl+C") && api.copyItemToClipboard) api.copyItemToClipboard(item, false);
            if (menuItem(popupTheme, Icons::Cut, tr("Cut"), "Ctrl+X") && api.copyItemToClipboard) api.copyItemToClipboard(item, true);
            ImGui::Separator();
            if (menuItem(popupTheme, Icons::Edit, tr("Edit"), "F2") && api.openItemEditor) api.openItemEditor(context, itemIndex);
            if (menuItem(popupTheme, Icons::Delete, tr("Delete"), "Del") && api.requestDeleteSelection) {
                api.requestDeleteSelection(context);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            if (animatedPopup) {
                ui_anim::popAppearAlpha();
            }
            return;
        }

        if (menuItem(popupTheme, Icons::RunAsAdmin, tr("Run as Administrator"), "Ctrl+Shift+Enter", false,
                     item.type != LaunchItemType::VirtualFolder && item.type != LaunchItemType::Note) &&
            api.runItem) {
            const bool old = item.runAsAdmin;
            item.runAsAdmin = true;
            api.runItem(context, item, SW_SHOWNORMAL);
            item.runAsAdmin = old;
        }
        if (menuItem(popupTheme, Icons::MinimizeRun, tr("Run Minimized"), nullptr, false,
                     item.type != LaunchItemType::VirtualFolder && item.type != LaunchItemType::Note) &&
            api.runItem) {
            api.runItem(context, item, SW_SHOWMINIMIZED);
        }
        if (menuItem(popupTheme, Icons::OpenWith, tr("Open With"), "Ctrl+Enter", false,
                     item.type != LaunchItemType::VirtualFolder && item.type != LaunchItemType::Note && !item.target.empty()) &&
            api.openWithDialog) {
            api.openWithDialog(item);
        }
        if (menuItem(popupTheme, Icons::Folder, tr("Open Containing Folder"), "Ctrl+Shift+E", false,
                     item.type != LaunchItemType::Note && !item.target.empty()) &&
            api.openContainingFolder) {
            api.openContainingFolder(item);
        }
        if (menuItem(popupTheme, Icons::Folder, tr("Explorer Menu"), nullptr, false,
                     item.type != LaunchItemType::Note && !item.target.empty()) &&
            api.showFileProperties) {
            api.showFileProperties(item);
        }
        ImGui::Separator();
        if (menuItem(popupTheme, Icons::Refresh, tr("Rebuild Icon Cache")) && api.rebuildIconCache) {
            api.rebuildIconCache(context, item);
        }
        if (menuItem(popupTheme, Icons::Task, tr("Add to Task Planner")) && api.addScheduledTask) {
            api.addScheduledTask(context, item);
            ImGui::CloseCurrentPopup();
        }
        if (beginIconMenu(popupTheme, Icons::CopyProperties, tr("Copy Properties"))) {
            if (menuItem(popupTheme, "", tr("All")) && api.itemPropertiesText && api.copyTextToClipboard)
                api.copyTextToClipboard(api.itemPropertiesText(item));
            if (menuItem(popupTheme, "", tr("Name")) && api.copyTextToClipboard) api.copyTextToClipboard(item.name);
            if (menuItem(popupTheme, "", tr("Target")) && api.copyTextToClipboard) api.copyTextToClipboard(item.target.string());
            if (menuItem(popupTheme, "", tr("Start Directory")) && api.copyTextToClipboard)
                api.copyTextToClipboard(item.startDirectory.string());
            if (menuItem(popupTheme, "", tr("Arguments")) && api.copyTextToClipboard) api.copyTextToClipboard(item.arguments);
            if (menuItem(popupTheme, "", tr("Icon")) && api.copyTextToClipboard) api.copyTextToClipboard(item.icon);
            if (menuItem(popupTheme, "", tr("Search Keywords")) && api.copyTextToClipboard) api.copyTextToClipboard(item.keywords);
            if (menuItem(popupTheme, "", tr("Remark")) && api.copyTextToClipboard) api.copyTextToClipboard(item.remark);
            endIconMenu();
        }
        ImGui::Separator();
        if (menuItem(popupTheme, Icons::CopyPath, tr("Copy Path"), "Ctrl+Shift+C")) ImGui::SetClipboardText(item.target.string().c_str());
        if (menuItem(popupTheme, Icons::Copy, tr("Copy"), "Ctrl+C") && api.copyItemToClipboard) api.copyItemToClipboard(item, false);
        if (menuItem(popupTheme, Icons::Cut, tr("Cut"), "Ctrl+X") && api.copyItemToClipboard) api.copyItemToClipboard(item, true);
        ImGui::Separator();
        if (menuItem(popupTheme, Icons::Edit, tr("Edit"), "F2") && api.openItemEditor) api.openItemEditor(context, itemIndex);
        if (menuItem(popupTheme, Icons::Delete, tr("Delete"), "Del") && api.requestDeleteSelection) {
            api.requestDeleteSelection(context);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (animatedPopup) {
        ui_anim::popAppearAlpha();
    }
}

void drawItemTooltip(const UiPalette& theme, const AppSettings& settings, const LaunchItem& item, const ImRect& itemRect,
                     const TooltipApi& api)
{
    if (!settings.tooltipEnabled) {
        return;
    }
    if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
        ImGui::SetNextWindowViewport(viewport->ID);
        const ImVec2 expectedSize(260.0f, 120.0f);
        ImVec2 pos(itemRect.Min.x, itemRect.Max.y + 6.0f);
        pos.x = std::clamp(pos.x, viewport->WorkPos.x + 6.0f, viewport->WorkPos.x + viewport->WorkSize.x - expectedSize.x - 6.0f);
        pos.y = std::min(pos.y, viewport->WorkPos.y + viewport->WorkSize.y - expectedSize.y - 6.0f);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    }
    const UiPalette tooltipTheme = withPopupOpacity(theme, settings.itemTooltipOpacity);
    beginStyledTooltip(tooltipTheme, ImGui::GetID(("tooltip-" + item.id).c_str()), 360.0f, 0.10f, 0.12f, settings.itemTooltipOpacity);
    ImGui::TextUnformatted(item.name.c_str());
    ImGui::Separator();
    if (settings.tooltipRunCount) ImGui::Text(tr("Run count: %d"), item.runCount);
    if (settings.tooltipTarget && !item.target.empty()) ImGui::Text(tr("Target: %s"), item.target.string().c_str());
    if (settings.tooltipArguments && !item.arguments.empty()) ImGui::Text(tr("Arguments: %s"), item.arguments.c_str());
    if (settings.tooltipRemark && !item.remark.empty()) ImGui::Text(tr("Remark: %s"), item.remark.c_str());
    if (api.timeText) {
        if (settings.tooltipCreatedAt) ImGui::Text(tr("Created at: %s"), api.timeText(item.createdAt).c_str());
        if (settings.tooltipLastEditedAt) ImGui::Text(tr("Last edited at: %s"), api.timeText(item.lastEditedAt).c_str());
        if (settings.tooltipLastRunAt) ImGui::Text(tr("Last run at: %s"), api.timeText(item.lastRunAt).c_str());
    }
    endStyledTooltip();
}

} // namespace launcher

#pragma once

#include "launcher/Models.hpp"
#include "ui/UiTheme.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>
#include <string>
#include <vector>

namespace launcher {

struct AppContext;

struct ContentMenuApi {
    void (*drawViewMenu)(const UiPalette&, AppContext&) = nullptr;
    void (*drawSortMenu)(const UiPalette&, AppContext&) = nullptr;
    std::vector<LaunchItem>* (*currentItems)(AppContext&) = nullptr;
    void (*pasteClipboardItem)(AppContext&) = nullptr;
    void (*openItemEditor)(AppContext&, int, LaunchItemType) = nullptr;
    void (*appendPlaceholders)(AppContext&, int) = nullptr;
    void (*appendItem)(AppContext&, LaunchItemType, const std::string&) = nullptr;
    void (*appendNoteItem)(AppContext&) = nullptr;
    void (*appendBuiltInItem)(AppContext&, const std::string&, const std::string&, const std::string&) = nullptr;
    void (*runItemsInList)(AppContext&, std::vector<LaunchItem>&) = nullptr;
    void (*requestHideMainWindow)() = nullptr;
    void (*clearAllLinkIcons)() = nullptr;
    void (*requestDeleteIds)(const AppContext&, std::vector<std::string>) = nullptr;
};

struct ContentMenuState {
    bool clipboardAvailable = false;
};

struct TooltipApi {
    std::string (*timeText)(std::int64_t) = nullptr;
};

struct ItemMenuApi {
    std::function<bool(const LaunchItem&)> isItemSelected;
    std::function<void(AppContext&, const LaunchItem&)> selectSingle;
    std::function<void(AppContext&, int)> openItemEditor;
    std::function<void(const AppContext&)> requestDeleteSelection;
    std::function<void(AppContext&, const LaunchItem&)> enterVirtualFolder;
    std::function<void(const LaunchItem&, bool)> copyItemToClipboard;
    std::function<void(const std::string&)> copyTextToClipboard;
    std::function<std::string(const LaunchItem&)> itemPropertiesText;
    std::function<void(AppContext&, LaunchItem&, int)> runItem;
    std::function<void(AppContext&, const LaunchItem&)> openNoteEditor;
    std::function<void(const LaunchItem&)> openWithDialog;
    std::function<void(const LaunchItem&)> openContainingFolder;
    std::function<void(const LaunchItem&)> showFileProperties;
    std::function<void(AppContext&, const LaunchItem&)> rebuildIconCache;
    std::function<void(AppContext&, const LaunchItem&)> addScheduledTask;
};

void drawContentMenu(const UiPalette& theme, AppContext& context, const ContentMenuState& state, const ContentMenuApi& api);
void drawItemMenu(const UiPalette& theme, AppContext& context, std::vector<LaunchItem>& items, int itemIndex, const ItemMenuApi& api);
void drawItemTooltip(const UiPalette& theme, const AppSettings& settings, const LaunchItem& item, const ImRect& itemRect,
                     const TooltipApi& api);

} // namespace launcher

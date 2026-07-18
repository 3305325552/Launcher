#pragma once

#include "launcher/Models.hpp"
#include "ui/dock/MainDockCallbacks.hpp"
#include "ui/common/UiTheme.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdint>
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
    main_dock::Callback<bool(const LaunchItem&)> isItemSelected = nullptr;
    main_dock::Callback<void(AppContext&, const LaunchItem&)> selectSingle = nullptr;
    main_dock::Callback<void(AppContext&, int)> openItemEditor = nullptr;
    main_dock::Callback<void(const AppContext&)> requestDeleteSelection = nullptr;
    main_dock::Callback<void(AppContext&, const LaunchItem&)> enterVirtualFolder = nullptr;
    main_dock::Callback<void(const LaunchItem&, bool)> copyItemToClipboard = nullptr;
    main_dock::Callback<void(const std::string&)> copyTextToClipboard = nullptr;
    main_dock::Callback<std::string(const LaunchItem&)> itemPropertiesText = nullptr;
    main_dock::Callback<void(AppContext&, LaunchItem&, int)> runItem = nullptr;
    main_dock::Callback<void(AppContext&, const LaunchItem&)> openNoteEditor = nullptr;
    main_dock::Callback<void(const LaunchItem&)> openWithDialog = nullptr;
    main_dock::Callback<void(const LaunchItem&)> openContainingFolder = nullptr;
    main_dock::Callback<void(const LaunchItem&)> showFileProperties = nullptr;
    main_dock::Callback<void(AppContext&, const LaunchItem&)> rebuildIconCache = nullptr;
    main_dock::Callback<void(AppContext&, const LaunchItem&)> addScheduledTask = nullptr;
};

void drawContentMenu(const UiPalette& theme, AppContext& context, const ContentMenuState& state, const ContentMenuApi& api);
void drawItemMenu(const UiPalette& theme, AppContext& context, std::vector<LaunchItem>& items, int itemIndex, const ItemMenuApi& api);
void drawItemTooltip(const UiPalette& theme, const AppSettings& settings, const LaunchItem& item, const ImRect& itemRect,
                     const TooltipApi& api);

} // namespace launcher

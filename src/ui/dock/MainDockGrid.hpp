#pragma once

#include "launcher/Models.hpp"
#include "ui/dock/MainDockCallbacks.hpp"
#include "ui/dock/MainDockContextMenus.hpp"
#include "ui/dock/MainDockItemViews.hpp"
#include "ui/common/UiTheme.hpp"

#include <imgui.h>

#include <string>
#include <vector>

namespace launcher {

struct AppContext;

struct MainDockGridState {
    std::vector<std::string>* folderStack = nullptr;
    bool* resetMainDockScroll = nullptr;
};

struct MainDockGridApi {
    main_dock::Callback<std::vector<LaunchItem>*(AppContext&)> currentItems = nullptr;
    main_dock::Callback<std::vector<LaunchItem>*(AppContext&, const std::vector<std::string>&)> itemsForFolderStack = nullptr;
    main_dock::Callback<void(AppContext&)> clearSelection = nullptr;
    main_dock::Callback<void()> requestHideMainWindow = nullptr;
    main_dock::Callback<int(const std::vector<LaunchItem>&, const std::string&)> itemIndexById = nullptr;
    main_dock::Callback<bool(AppContext&, const std::string&, std::vector<LaunchItem>&, int)> moveItemByIdToList = nullptr;
    main_dock::Callback<bool(AppContext&, const std::vector<std::string>&, std::vector<LaunchItem>&, int)> moveItemIdsToList = nullptr;
    main_dock::Callback<LaunchItem*(AppContext&, const std::string&)> findItemById = nullptr;
    main_dock::Callback<std::string(const ImGuiPayload*)> dragPayloadId = nullptr;
    main_dock::Callback<std::vector<std::string>(const AppContext&, const std::string&)> dragItemIds = nullptr;
    main_dock::Callback<void(const std::string&, const main_dock::DeferredCallback&)> triggerAfterDragHover = nullptr;
    main_dock::Callback<bool(const std::string&)> dragHoverPending = nullptr;
    main_dock::Callback<void(const UiPalette&, AppContext&, const ContentMenuState&, const ContentMenuApi&)> drawContentMenu = nullptr;
    ContentMenuApi contentMenuApi;
    ContentMenuState (*contentMenuState)() = nullptr;
    ItemViewApi itemViewApi;
};

void drawItemGrid(const UiPalette& theme, AppContext& context, MainDockGridState state, const MainDockGridApi& api, const ImVec2& origin,
                  const ImVec2& size);
void captureActiveDragPreviewForDrop(const AppContext& context);
void rememberDragSourceVisualPosition(const LaunchItem& item, const ImVec2& pos);
void clearMainDockDragVisualState();

} // namespace launcher

#pragma once

#include "launcher/Models.hpp"
#include "ui/dock/MainDockContextMenus.hpp"
#include "ui/dock/MainDockItemViews.hpp"
#include "ui/common/UiTheme.hpp"

#include <functional>
#include <string>
#include <vector>

namespace launcher {

struct AppContext;

struct MainDockGridState {
    std::vector<std::string>* folderStack = nullptr;
    bool* resetMainDockScroll = nullptr;
};

struct MainDockGridApi {
    std::function<std::vector<LaunchItem>*(AppContext&)> currentItems;
    std::function<std::vector<LaunchItem>*(AppContext&, const std::vector<std::string>&)> itemsForFolderStack;
    std::function<void(AppContext&)> clearSelection;
    std::function<void()> requestHideMainWindow;
    std::function<int(const std::vector<LaunchItem>&, const std::string&)> itemIndexById;
    std::function<bool(AppContext&, const std::string&, std::vector<LaunchItem>&, int)> moveItemByIdToList;
    std::function<bool(AppContext&, const std::vector<std::string>&, std::vector<LaunchItem>&, int)> moveItemIdsToList;
    std::function<const LaunchItem*(AppContext&, const std::string&)> findItemById;
    std::function<std::string(const ImGuiPayload*)> dragPayloadId;
    std::function<std::vector<std::string>(const AppContext&, const std::string&)> dragItemIds;
    std::function<void(const std::string&, const std::function<void()>&)> triggerAfterDragHover;
    std::function<bool(const std::string&)> dragHoverPending;
    std::function<void(const UiPalette&, AppContext&, const ContentMenuState&, const ContentMenuApi&)> drawContentMenu;
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

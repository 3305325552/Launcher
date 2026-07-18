#pragma once

#include "launcher/AppSettings.hpp"
#include "launcher/Models.hpp"
#include "ui/dock/MainDockCallbacks.hpp"
#include "ui/common/UiTheme.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>
#include <vector>

namespace launcher {

struct AppContext;

float itemViewTitleRowHeight(const LaunchItem& item);

struct ItemViewApi {
    main_dock::Callback<bool(const LaunchItem&)> isItemSelected = nullptr;
    main_dock::Callback<void(AppContext&, const std::vector<LaunchItem>&, int)> handleItemSelectionClick = nullptr;
    main_dock::Callback<void(AppContext&, const LaunchItem&)> selectSingle = nullptr;
    main_dock::Callback<void(AppContext&, const LaunchItem&)> enterVirtualFolder = nullptr;
    main_dock::Callback<void(AppContext&, LaunchItem&, int)> runItem = nullptr;
    main_dock::Callback<void(const UiPalette&, AppContext&, std::vector<LaunchItem>&, int)> drawItemMenu = nullptr;
    main_dock::Callback<void(const UiPalette&, const AppSettings&, const LaunchItem&, const ImRect&)> drawItemTooltip = nullptr;
    main_dock::Callback<void(const LaunchItem&, const ImVec2&, float)> drawLaunchIcon = nullptr;
    main_dock::Callback<void(ImDrawList*, const LaunchItem&, const ImVec2&, float)> drawLaunchIconOnList = nullptr;
    main_dock::Callback<std::string(const ImGuiPayload*)> dragPayloadId = nullptr;
    main_dock::Callback<void(const AppContext&, const std::string&)> captureDragItemIds = nullptr;
    main_dock::Callback<void(const std::string&, const main_dock::DeferredCallback&)> triggerAfterDragHover = nullptr;
    main_dock::Callback<bool(const std::string&)> dragHoverPending = nullptr;
    main_dock::Callback<void(const LaunchItem&)> showFileProperties = nullptr;
};

void drawTitleRow(const UiPalette& theme, AppContext& context, const ItemViewApi& api, std::vector<LaunchItem>& items, int itemIndex,
                  const ImVec2& row, float width);
void drawIconTile(const UiPalette& theme, AppContext& context, const ItemViewApi& api, std::vector<LaunchItem>& items, int itemIndex,
                  const ImVec2& tile, const ImVec2& tileSize, float iconSize);
void drawListItem(const UiPalette& theme, AppContext& context, const ItemViewApi& api, std::vector<LaunchItem>& items, int itemIndex,
                  const ImVec2& row, float width, float height, float iconSize);

} // namespace launcher

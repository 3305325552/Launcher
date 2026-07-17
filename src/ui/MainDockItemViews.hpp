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

float itemViewTitleRowHeight(const LaunchItem& item);

struct ItemViewApi {
    std::function<bool(const LaunchItem&)> isItemSelected;
    std::function<void(AppContext&, const std::vector<LaunchItem>&, int)> handleItemSelectionClick;
    std::function<void(AppContext&, const LaunchItem&)> selectSingle;
    std::function<void(AppContext&, const LaunchItem&)> enterVirtualFolder;
    std::function<void(AppContext&, LaunchItem&, int)> runItem;
    std::function<void(const UiPalette&, AppContext&, std::vector<LaunchItem>&, int)> drawItemMenu;
    std::function<void(const UiPalette&, const AppSettings&, const LaunchItem&, const ImRect&)> drawItemTooltip;
    std::function<void(const LaunchItem&, const ImVec2&, float)> drawLaunchIcon;
    std::function<void(ImDrawList*, const LaunchItem&, const ImVec2&, float)> drawLaunchIconOnList;
    std::function<std::string(const ImGuiPayload*)> dragPayloadId;
    std::function<void(const AppContext&, const std::string&)> captureDragItemIds;
    std::function<void(const std::string&, const std::function<void()>&)> triggerAfterDragHover;
    std::function<bool(const std::string&)> dragHoverPending;
    std::function<void(const LaunchItem&)> showFileProperties;
};

void drawTitleRow(const UiPalette& theme, AppContext& context, const ItemViewApi& api, std::vector<LaunchItem>& items, int itemIndex,
                  const ImVec2& row, float width);
void drawIconTile(const UiPalette& theme, AppContext& context, const ItemViewApi& api, std::vector<LaunchItem>& items, int itemIndex,
                  const ImVec2& tile, const ImVec2& tileSize, float iconSize);
void drawListItem(const UiPalette& theme, AppContext& context, const ItemViewApi& api, std::vector<LaunchItem>& items, int itemIndex,
                  const ImVec2& row, float width, float height, float iconSize);

} // namespace launcher

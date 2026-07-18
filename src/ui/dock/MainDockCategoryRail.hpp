#pragma once

#include "launcher/Models.hpp"
#include "ui/dock/MainDockCallbacks.hpp"
#include "ui/common/UiTheme.hpp"

#include <imgui.h>

#include <string>
#include <vector>

namespace launcher {

struct AppContext;

struct CategoryRailState {
    std::vector<std::string>* folderStack = nullptr;
    bool* openCategoryEditorPopup = nullptr;
    int* editingCategoryIndex = nullptr;
    std::string* editingCategoryName = nullptr;
    std::string* editingCategoryIconName = nullptr;
    std::string* editingCategoryIconColor = nullptr;
    std::string* categoryIconFilter = nullptr;
};

struct CategoryRailApi {
    main_dock::Callback<void(AppContext&)> clearSelection = nullptr;
    main_dock::Callback<void(AppContext&, int)> openItemEditor = nullptr;
    main_dock::Callback<void(const AppContext&, int)> requestDeleteCategory = nullptr;
    main_dock::Callback<void(AppContext&, int, int)> reorderCategory = nullptr;
    main_dock::Callback<bool(AppContext&, const std::string&, std::vector<LaunchItem>&)> moveItemByIdToList = nullptr;
    main_dock::Callback<bool(AppContext&, const std::vector<std::string>&, std::vector<LaunchItem>&)> moveItemIdsToList = nullptr;
    main_dock::Callback<void(const std::string&, const main_dock::DeferredCallback&)> triggerAfterDragHover = nullptr;
    main_dock::Callback<bool(const std::string&)> dragHoverPending = nullptr;
    main_dock::Callback<std::string(const ImGuiPayload*)> dragPayloadId = nullptr;
    main_dock::Callback<std::vector<std::string>(const AppContext&, const std::string&)> dragItemIds = nullptr;
    main_dock::Callback<void(AppContext&, std::vector<LaunchItem>&)> runItemsInList = nullptr;
    main_dock::Callback<void()> requestHideMainWindow = nullptr;
};

void drawCategoryRail(const UiPalette& theme, AppContext& context, CategoryRailState state, const CategoryRailApi& api,
                      const ImVec2& origin, float height, float railWidth);

} // namespace launcher

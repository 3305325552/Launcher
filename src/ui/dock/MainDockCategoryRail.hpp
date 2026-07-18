#pragma once

#include "launcher/Models.hpp"
#include "ui/common/UiTheme.hpp"

#include <imgui.h>

#include <functional>
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
    std::function<void(AppContext&)> clearSelection;
    std::function<void(AppContext&, int)> openItemEditor;
    std::function<void(const AppContext&, int)> requestDeleteCategory;
    std::function<void(AppContext&, int, int)> reorderCategory;
    std::function<bool(AppContext&, const std::string&, std::vector<LaunchItem>&)> moveItemByIdToList;
    std::function<bool(AppContext&, const std::vector<std::string>&, std::vector<LaunchItem>&)> moveItemIdsToList;
    std::function<void(const std::string&, const std::function<void()>&)> triggerAfterDragHover;
    std::function<bool(const std::string&)> dragHoverPending;
    std::function<std::string(const ImGuiPayload*)> dragPayloadId;
    std::function<std::vector<std::string>(const AppContext&, const std::string&)> dragItemIds;
    std::function<void(AppContext&, std::vector<LaunchItem>&)> runItemsInList;
    std::function<void()> requestHideMainWindow;
};

void drawCategoryRail(const UiPalette& theme, AppContext& context, CategoryRailState state, const CategoryRailApi& api,
                      const ImVec2& origin, float height, float railWidth);

} // namespace launcher

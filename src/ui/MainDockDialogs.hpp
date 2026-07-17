#pragma once

#include "ui/UiTheme.hpp"

#include <string>
#include <vector>

namespace launcher {

struct AppContext;

struct DeleteConfirmState {
    bool openNextFrame = false;
    std::vector<std::string>* pendingDeleteIds = nullptr;
    std::vector<std::string>* pendingDeleteNames = nullptr;
    int* pendingDeleteCategory = nullptr;
    std::string* pendingDeleteCategoryName = nullptr;
};

void drawBuildInfoPopup(const ThemeDefinition& themeDefinition, const UiPalette& theme, bool& showBuildInfo, void (*openAppFolderFn)());
void drawDeleteConfirmPopup(const UiPalette& theme, AppContext& context, DeleteConfirmState state,
                            void (*deletePendingItemsFn)(AppContext&));
void drawTaskPlannerWindow(AppContext& context, const ThemeDefinition& themeDefinition, const UiPalette& theme, bool& showTaskPlanner);

} // namespace launcher

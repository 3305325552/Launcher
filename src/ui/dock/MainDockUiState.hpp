#pragma once

#include "launcher/Models.hpp"
#include "ui/dock/MainDockSearch.hpp"
#include "ui/common/UiTheme.hpp"

#include <imgui.h>
#include <windows.h>

#include <string>
#include <vector>

namespace launcher {

// Per-window UI state. Persistent application data remains in AppContext; this
// state only tracks transient interaction and popup data for the main Dock.
struct MainDockSession {
    bool focusSearch = false;
    bool searchOpen = false;
    bool showItemEditor = false;
    bool openItemEditorPopup = false;
    bool openSettingsNextFrame = false;
    int editingCategory = -1;
    int editingItem = -1;
    std::string editingFolderId;
    LaunchItem editingDraft;
    std::string editingTarget;
    std::string editingStartDir;
    std::string editingRemark;
    std::string editingIcon;
    bool showInteractiveRun = false;
    bool openInteractiveRunPopup = false;
    LaunchItem interactiveRunItem;
    std::string interactiveRunItemId;
    std::string interactiveRunSearchText;
    int interactiveRunShowCommand = SW_SHOWNORMAL;
    std::vector<std::string> interactiveRunValues;
    std::string interactiveHistoryParamKey;
    int interactiveHistorySelected = -1;
    bool searchSubmit = false;
    int searchSelected = 0;
    int searchMove = 0;
    int searchPageMove = 0;
    std::string searchQueryText;
    double searchEditedAt = 0.0;
    bool searchCursorEndRequested = false;
    SearchResultsCache searchResultsCache;
    bool showBuildInfo = false;
    bool showTaskPlanner = false;
    bool showUpdateDialog = false;
    std::string automaticUpdatePromptVersion;
    bool openCategoryEditorPopup = false;
    int editingCategoryIndex = -1;
    std::string editingCategoryName;
    std::string editingCategoryIconName;
    std::string editingCategoryIconColor;
    std::string categoryIconFilter;
    bool draggingMainWindow = false;
    ImVec2 mainDragStartMouse{};
    RECT mainDragStartRect{};
    bool resetMainDockScroll = false;
    bool themeEditorWasOpen = false;
    bool useDefaultIcons = false;
    UiPalette theme = uiPalette(ThemeDefinition{});
};

} // namespace launcher

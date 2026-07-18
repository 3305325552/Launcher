#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace launcher {

enum class ItemViewMode {
    Icon,
    List,
    Tile
};

enum class SortMode {
    Free,
    Name,
    Type,
    RunCount,
    CreatedAt,
    LastRunAt,
    LastEditedAt
};

struct PluginPreference {
    std::string id;
    bool enabled = false;
    std::map<std::string, std::string> settings;
};

struct AppSettings {
    std::string language = "zh-CN";
    bool startHidden = false;
    bool alwaysOnTop = false;
    bool lockLayout = false;
    bool lockWindowPosition = false;
    bool lockWindowSize = false;
    bool lockItemLayout = false;
    bool dragBlankAreaMoveWindow = false;
    bool enableDocking = false;
    bool enableViewport = true;
    bool startWithWindows = false;
    bool minimizeToTray = true;
    bool showTaskbarIcon = false;
    bool fullscreenDoNotDisturb = true;
    bool enableGlobalHotkey = true;
    std::string globalHotkey = "Ctrl+Alt+Space";
    bool enableSearchHotkey = false;
    std::string searchHotkey = "Ctrl+Alt+F";
    std::string themeId = "builtin:dark";
    bool showSearchButton = true;
    bool showMenuButton = true;
    bool showCloseButton = true;
    int iconSize = 48;
    bool useDefaultIcons = false;
    int searchDelayMs = 180;
    ItemViewMode viewMode = ItemViewMode::Icon;
    SortMode sortMode = SortMode::Free;
    int nameLines = 1;
    bool tooltipEnabled = true;
    bool tooltipFollowMouse = false;
    bool tooltipRunCount = true;
    bool tooltipTarget = true;
    bool tooltipArguments = true;
    bool tooltipRemark = true;
    bool tooltipCreatedAt = false;
    bool tooltipLastEditedAt = false;
    bool tooltipLastRunAt = false;
    int itemTooltipOpacity = 100;
    bool showMenuShortcutHints = true;
    bool runItemHidesMain = false;
    bool closeSearchAfterRun = true;
    bool hideSearchAfterMainClose = true;
    bool clearSelectionAfterMainClose = true;
    bool searchAltNumberRun = true;
    bool smoothInput = true;
    bool autoUpdateEnvironment = true;
    bool autoCheckUpdates = true;
    bool checkUpdatesAtStartup = true;
    bool checkUpdatesDaily = true;
    std::int64_t lastUpdateCheckAt = 0;
    bool dragEnhanced = true;
    bool dragSwapPlaceholder = true;
    bool doubleClickRun = true;
    bool keepScrollAfterWake = true;
    bool middleClickRunsCategory = true;
    bool virtualFolderRunsAll = true;
    bool shiftRightClickExplorerMenu = true;
    bool directorySearchContextMenu = true;
    bool wakeFollowMouse = false;
    bool magneticScreenCorner = false;
    bool magneticScreenEdge = false;
    bool wakeByTrayHover = true;
    bool hideOnMouseLeave = false;
    bool hideOnFocusLost = false;
    bool hideOnBlankDoubleClick = false;
    bool hideAfterRun = true;
    bool searchRegex = true;
    bool searchPinyinInitial = true;
    bool searchPinyin = true;
    bool searchEnglishMode = false;
    bool searchScopeTarget = false;
    bool searchScopeRemark = false;
    bool searchParamVariable = true;
    bool advancedSearch = false;
    bool enableGlobalSearch = false;
    bool globalSearchHideSystemPaths = true;
    // 0=Low, 1=Balanced, 2=High. Higher values still stay modest.
    int globalSearchScanIntensity = 1;
    int searchResultLimit = 128;
    int globalSearchResultLimit = 80;
    bool enableAnimations = true;
    int animationSpeedPercent = 100;
    std::vector<PluginPreference> plugins;
};

} // namespace launcher

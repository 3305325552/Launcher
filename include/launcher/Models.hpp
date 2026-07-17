#pragma once

#include <filesystem>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace launcher {

enum class LaunchItemType {
    App,
    Url,
    Script,
    BuiltIn,
    Placeholder,
    Title,
    VirtualFolder,
    Note
};

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

enum class InteractiveParamKind {
    Text,
    Number,
    Choice
};

enum class ScheduledTriggerKind {
    Once,
    Daily,
    Weekly,
    Interval,
    AppStart,
    WakeUnlock,
    ProcessStart
};

enum class ScheduledActionKind {
    LaunchItem,
    LaunchVirtualFolder
};

struct ScheduledTaskHistory {
    std::int64_t startedAt = 0;
    std::int64_t finishedAt = 0;
    bool success = false;
    std::string message;
};

struct ScheduledTask {
    std::string id;
    std::string name;
    bool enabled = true;
    ScheduledTriggerKind trigger = ScheduledTriggerKind::Daily;
    ScheduledActionKind action = ScheduledActionKind::LaunchItem;
    std::string itemId;
    int hour = 9;
    int minute = 0;
    int weekdayMask = 0x7F;
    int intervalMinutes = 60;
    std::int64_t onceAt = 0;
    std::string processName;
    bool runMissed = false;
    bool confirmBeforeRun = false;
    bool runMinimized = false;
    int retryCount = 0;
    int retryDelaySeconds = 60;
    int timeoutSeconds = 0;
    std::int64_t lastRunAt = 0;
    std::int64_t nextRunAt = 0;
    std::int64_t retryAt = 0;
    int pendingRetries = 0;
    bool lastSuccess = false;
    std::string lastMessage;
    std::vector<ScheduledTaskHistory> history;
};

struct InteractiveParamHistory {
    std::string value;
    int useCount = 0;
    std::int64_t lastUsedAt = 0;
};

struct InteractiveParam {
    std::string id;
    std::string label;
    InteractiveParamKind kind = InteractiveParamKind::Text;
    std::string defaultValue;
    double minValue = 0.0;
    double maxValue = 100.0;
    double step = 1.0;
    std::vector<std::string> choices;
    std::vector<InteractiveParamHistory> history;
};

struct LaunchItem {
    std::string id;
    std::string name;
    std::string subtitle;
    std::filesystem::path target;
    std::filesystem::path startDirectory;
    std::string arguments;
    std::string icon;
    std::string fallbackColor = "#8C8C8CFF";
    std::string keywords;
    std::string hotkey;
    std::string remark;
    LaunchItemType type = LaunchItemType::App;
    bool runAsAdmin = false;
    bool hotkeyInputMode = false;
    int priority = 2;
    int titleSize = 18;
    int titleAlign = 0;
    bool lockLayout = false;
    int runCount = 0;
    std::int64_t createdAt = 0;
    std::int64_t lastEditedAt = 0;
    std::int64_t lastRunAt = 0;
    std::vector<LaunchItem> children;
    bool interactive = false;
    std::vector<InteractiveParam> interactiveParams;
};

struct Category {
    std::string id;
    std::string name;
    std::string iconName;
    std::vector<LaunchItem> items;
    std::string iconColor = "#FFFFFFFF";
    bool useGlobalLayout = true;
    int iconSize = 48;
    ItemViewMode viewMode = ItemViewMode::Icon;
    int nameLines = 1;
    bool lockLayout = false;
};

struct ThemeColor {
    std::string key;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct ThemeBackground {
    bool enabled = false;
    std::filesystem::path imagePath;
    std::string imageEmbeddedName;
    std::string imageEmbeddedMime;
    std::string imageEmbeddedBase64;
    int imageMode = 0;
    int opacity = 100;
    bool animated = false;
    int animationFps = 15;
    int animationMaxWidth = 960;
    int animationQuality = 8;
};

struct ThemeDefinition {
    std::string id = "builtin:dark";
    std::string name = "Dark";
    std::string author = "Launcher";
    bool dark = true;
    bool windowTransparent = false;
    int windowOpacity = 100;
    bool windowTransparencyForAll = false;
    int secondaryWindowOpacity = 100;
    int popupMenuOpacity = 100;
    ThemeBackground background;
    std::vector<ThemeColor> colors;
    std::filesystem::path sourcePath;
    bool builtin = true;
};

struct ThemeCatalogEntry {
    std::string id;
    std::string name;
    std::string author;
    std::filesystem::path path;
    bool builtin = false;
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
    bool tooltipRunCount = true;
    bool tooltipTarget = true;
    bool tooltipArguments = true;
    bool tooltipRemark = true;
    bool tooltipCreatedAt = false;
    bool tooltipLastEditedAt = false;
    bool tooltipLastRunAt = false;
    int itemTooltipOpacity = 100;
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

struct PersistedState {
    AppSettings settings;
    std::vector<Category> categories;
    std::vector<ScheduledTask> scheduledTasks;
};

struct RuntimeState {
    std::string searchText;
    std::string selectedItemId;
    int selectedCategory = 0;
    std::vector<std::string> currentFolderStack;
    bool showSettings = false;
    bool showThemeEditor = false;
    bool showUserGuide = false;
    bool showNotes = false;
    bool showNoteQuick = false;
    bool editSelectedNote = false;
    std::string selectedNoteId;
};

struct AppState {
    PersistedState persistedState;
    RuntimeState runtimeState;

    PersistedState& persisted()
    {
        return persistedState;
    }

    const PersistedState& persisted() const
    {
        return persistedState;
    }

    RuntimeState& runtime()
    {
        return runtimeState;
    }

    const RuntimeState& runtime() const
    {
        return runtimeState;
    }
};

} // namespace launcher

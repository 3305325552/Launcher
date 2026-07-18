#pragma once

#include "launcher/AppSettings.hpp"
#include "launcher/ThemeTypes.hpp"

#include <filesystem>
#include <cstdint>
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

#include "core/ConfigStore.hpp"

#include "launcher/AppIdentity.hpp"

#include <windows.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <exception>
#include <utility>

namespace launcher {

using json = nlohmann::json;

constexpr int kCurrentConfigSchemaVersion = 1;
constexpr const char* kSchemaVersionKey = "schemaVersion";
constexpr const char* kConfigFileName = "config.json";
constexpr const char* kConfigLocationFileName = "config-location.json";
constexpr const char* kConfigDirectoryKey = "configDirectory";

void migrateConfig(json& document)
{
    const int schemaVersion = document.value(kSchemaVersionKey, 0);
    if (schemaVersion < 1) {
        document[kSchemaVersionKey] = 1;
    }
}

std::filesystem::path localAppData()
{
    wchar_t buffer[MAX_PATH]{};
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(buffer);
}

std::filesystem::path legacyConfigPath()
{
    return localAppData() / app_identity::kLegacyConfigDirectoryName / kConfigFileName;
}

std::filesystem::path defaultConfigPath()
{
    return localAppData() / app_identity::kConfigDirectoryName / kConfigFileName;
}

bool samePath(const std::filesystem::path& lhs, const std::filesystem::path& rhs)
{
    std::error_code ec;
    if (std::filesystem::exists(lhs, ec) && std::filesystem::exists(rhs, ec)) {
        return std::filesystem::equivalent(lhs, rhs, ec) && !ec;
    }
    std::error_code lhsError;
    std::error_code rhsError;
    const std::filesystem::path absoluteLhs = std::filesystem::absolute(lhs, lhsError).lexically_normal();
    const std::filesystem::path absoluteRhs = std::filesystem::absolute(rhs, rhsError).lexically_normal();
    if (lhsError || rhsError) {
        return lhs.lexically_normal() == rhs.lexically_normal();
    }
    return absoluteLhs == absoluteRhs;
}

void setError(std::string* error, const std::string& message)
{
    if (error) {
        *error = message;
    }
}

std::filesystem::path configuredDirectory(const std::filesystem::path& fallbackDirectory, const std::filesystem::path& locationPath)
{
    std::ifstream input(locationPath);
    if (!input) {
        return fallbackDirectory;
    }

    try {
        json document;
        input >> document;
        const std::string configured = document.value(kConfigDirectoryKey, "");
        if (!configured.empty()) {
            return std::filesystem::path(configured);
        }
    } catch (...) {}
    return fallbackDirectory;
}

std::filesystem::path configuredConfigPath(const std::filesystem::path& fallbackConfigPath, const std::filesystem::path& locationPath)
{
    return configuredDirectory(fallbackConfigPath.parent_path(), locationPath) / kConfigFileName;
}

bool writeConfigLocation(const std::filesystem::path& defaultConfigPath, const std::filesystem::path& locationPath,
                         const std::filesystem::path& directory, std::string* error)
{
    std::error_code ec;
    std::filesystem::create_directories(locationPath.parent_path(), ec);
    if (ec) {
        setError(error, ec.message());
        return false;
    }

    if (samePath(directory, defaultConfigPath.parent_path())) {
        std::filesystem::remove(locationPath, ec);
        if (ec) {
            setError(error, ec.message());
            return false;
        }
        return true;
    }

    json document;
    document[kConfigDirectoryKey] = directory.string();
    std::ofstream output(locationPath, std::ios::binary);
    if (!output) {
        setError(error, "failed to write config location");
        return false;
    }
    output << document.dump(2);
    if (!output) {
        setError(error, "failed to write config location");
        return false;
    }
    return true;
}

void migrateLegacyConfigIfNeeded(const std::filesystem::path& configPath)
{
    std::error_code ec;
    if (std::filesystem::exists(configPath, ec)) {
        return;
    }
    const std::filesystem::path legacyPath = legacyConfigPath();
    if (!std::filesystem::exists(legacyPath, ec)) {
        return;
    }
    std::filesystem::create_directories(configPath.parent_path(), ec);
    std::filesystem::copy_file(legacyPath, configPath, std::filesystem::copy_options::overwrite_existing, ec);
}

std::string typeToString(LaunchItemType type)
{
    switch (type) {
    case LaunchItemType::Url: return "url";
    case LaunchItemType::Script: return "script";
    case LaunchItemType::BuiltIn: return "builtin";
    case LaunchItemType::Placeholder: return "placeholder";
    case LaunchItemType::Title: return "title";
    case LaunchItemType::VirtualFolder: return "virtual-folder";
    case LaunchItemType::Note: return "note";
    case LaunchItemType::App:
    default: return "app";
    }
}

LaunchItemType typeFromString(const std::string& value)
{
    if (value == "url") return LaunchItemType::Url;
    if (value == "script") return LaunchItemType::Script;
    if (value == "builtin") return LaunchItemType::BuiltIn;
    if (value == "placeholder") return LaunchItemType::Placeholder;
    if (value == "title") return LaunchItemType::Title;
    if (value == "virtual-folder") return LaunchItemType::VirtualFolder;
    if (value == "note") return LaunchItemType::Note;
    return LaunchItemType::App;
}

std::string viewModeToString(ItemViewMode mode)
{
    switch (mode) {
    case ItemViewMode::List: return "list";
    case ItemViewMode::Tile: return "tile";
    case ItemViewMode::Icon:
    default: return "icon";
    }
}

ItemViewMode viewModeFromString(const std::string& value)
{
    if (value == "list") return ItemViewMode::List;
    if (value == "tile") return ItemViewMode::Tile;
    return ItemViewMode::Icon;
}

std::string sortModeToString(SortMode mode)
{
    switch (mode) {
    case SortMode::Name: return "name";
    case SortMode::Type: return "type";
    case SortMode::RunCount: return "run-count";
    case SortMode::CreatedAt: return "created-at";
    case SortMode::LastRunAt: return "last-run-at";
    case SortMode::LastEditedAt: return "last-edited-at";
    case SortMode::Free:
    default: return "free";
    }
}

SortMode sortModeFromString(const std::string& value)
{
    if (value == "name") return SortMode::Name;
    if (value == "type") return SortMode::Type;
    if (value == "run-count") return SortMode::RunCount;
    if (value == "created-at") return SortMode::CreatedAt;
    if (value == "last-run-at") return SortMode::LastRunAt;
    if (value == "last-edited-at") return SortMode::LastEditedAt;
    return SortMode::Free;
}

std::string interactiveParamKindToString(InteractiveParamKind kind)
{
    switch (kind) {
    case InteractiveParamKind::Number: return "number";
    case InteractiveParamKind::Choice: return "choice";
    case InteractiveParamKind::Text:
    default: return "text";
    }
}

InteractiveParamKind interactiveParamKindFromString(const std::string& value)
{
    if (value == "number") return InteractiveParamKind::Number;
    if (value == "choice") return InteractiveParamKind::Choice;
    return InteractiveParamKind::Text;
}

std::string scheduledTriggerKindToString(ScheduledTriggerKind kind)
{
    switch (kind) {
    case ScheduledTriggerKind::Once: return "once";
    case ScheduledTriggerKind::Weekly: return "weekly";
    case ScheduledTriggerKind::Interval: return "interval";
    case ScheduledTriggerKind::AppStart: return "app-start";
    case ScheduledTriggerKind::WakeUnlock: return "wake-unlock";
    case ScheduledTriggerKind::ProcessStart: return "process-start";
    case ScheduledTriggerKind::Daily:
    default: return "daily";
    }
}

ScheduledTriggerKind scheduledTriggerKindFromString(const std::string& value)
{
    if (value == "once") return ScheduledTriggerKind::Once;
    if (value == "weekly") return ScheduledTriggerKind::Weekly;
    if (value == "interval") return ScheduledTriggerKind::Interval;
    if (value == "app-start") return ScheduledTriggerKind::AppStart;
    if (value == "wake-unlock") return ScheduledTriggerKind::WakeUnlock;
    if (value == "process-start") return ScheduledTriggerKind::ProcessStart;
    return ScheduledTriggerKind::Daily;
}

std::string scheduledActionKindToString(ScheduledActionKind kind)
{
    switch (kind) {
    case ScheduledActionKind::LaunchVirtualFolder: return "launch-virtual-folder";
    case ScheduledActionKind::LaunchItem:
    default: return "launch-item";
    }
}

ScheduledActionKind scheduledActionKindFromString(const std::string& value)
{
    if (value == "launch-virtual-folder") return ScheduledActionKind::LaunchVirtualFolder;
    return ScheduledActionKind::LaunchItem;
}

void to_json(json& j, const ScheduledTaskHistory& history)
{
    j = json{
        {"startedAt", history.startedAt}, {"finishedAt", history.finishedAt}, {"success", history.success}, {"message", history.message}};
}

void from_json(const json& j, ScheduledTaskHistory& history)
{
    history.startedAt = j.value("startedAt", 0LL);
    history.finishedAt = j.value("finishedAt", 0LL);
    history.success = j.value("success", false);
    history.message = j.value("message", "");
}

void to_json(json& j, const ScheduledTask& task)
{
    j = json{{"id", task.id},
             {"name", task.name},
             {"enabled", task.enabled},
             {"trigger", scheduledTriggerKindToString(task.trigger)},
             {"action", scheduledActionKindToString(task.action)},
             {"itemId", task.itemId},
             {"hour", task.hour},
             {"minute", task.minute},
             {"weekdayMask", task.weekdayMask},
             {"intervalMinutes", task.intervalMinutes},
             {"onceAt", task.onceAt},
             {"processName", task.processName},
             {"runMissed", task.runMissed},
             {"confirmBeforeRun", task.confirmBeforeRun},
             {"runMinimized", task.runMinimized},
             {"retryCount", task.retryCount},
             {"retryDelaySeconds", task.retryDelaySeconds},
             {"timeoutSeconds", task.timeoutSeconds},
             {"lastRunAt", task.lastRunAt},
             {"nextRunAt", task.nextRunAt},
             {"retryAt", task.retryAt},
             {"pendingRetries", task.pendingRetries},
             {"lastSuccess", task.lastSuccess},
             {"lastMessage", task.lastMessage},
             {"history", task.history}};
}

void from_json(const json& j, ScheduledTask& task)
{
    task.id = j.value("id", "");
    task.name = j.value("name", "");
    task.enabled = j.value("enabled", true);
    task.trigger = scheduledTriggerKindFromString(j.value("trigger", "daily"));
    task.action = scheduledActionKindFromString(j.value("action", "launch-item"));
    task.itemId = j.value("itemId", "");
    task.hour = std::clamp(j.value("hour", 9), 0, 23);
    task.minute = std::clamp(j.value("minute", 0), 0, 59);
    task.weekdayMask = std::clamp(j.value("weekdayMask", 0x7F), 0, 0x7F);
    task.intervalMinutes = std::clamp(j.value("intervalMinutes", 60), 1, 60 * 24 * 365);
    task.onceAt = j.value("onceAt", 0LL);
    task.processName = j.value("processName", "");
    task.runMissed = j.value("runMissed", false);
    task.confirmBeforeRun = j.value("confirmBeforeRun", false);
    task.runMinimized = j.value("runMinimized", false);
    task.retryCount = std::clamp(j.value("retryCount", 0), 0, 10);
    task.retryDelaySeconds = std::clamp(j.value("retryDelaySeconds", 60), 1, 3600);
    task.timeoutSeconds = std::clamp(j.value("timeoutSeconds", 0), 0, 24 * 60 * 60);
    task.lastRunAt = j.value("lastRunAt", 0LL);
    task.nextRunAt = j.value("nextRunAt", 0LL);
    task.retryAt = j.value("retryAt", 0LL);
    task.pendingRetries = std::clamp(j.value("pendingRetries", 0), 0, 10);
    task.lastSuccess = j.value("lastSuccess", false);
    task.lastMessage = j.value("lastMessage", "");
    task.history = j.value("history", std::vector<ScheduledTaskHistory>{});
}

void to_json(json& j, const InteractiveParamHistory& history)
{
    j = json{{"value", history.value}, {"useCount", history.useCount}, {"lastUsedAt", history.lastUsedAt}};
}

void from_json(const json& j, InteractiveParamHistory& history)
{
    history.value = j.value("value", "");
    history.useCount = j.value("useCount", 0);
    history.lastUsedAt = j.value("lastUsedAt", 0LL);
}

void to_json(json& j, const InteractiveParam& param)
{
    j = json{{"id", param.id},
             {"label", param.label},
             {"kind", interactiveParamKindToString(param.kind)},
             {"defaultValue", param.defaultValue},
             {"minValue", param.minValue},
             {"maxValue", param.maxValue},
             {"step", param.step},
             {"choices", param.choices},
             {"history", param.history}};
}

void from_json(const json& j, InteractiveParam& param)
{
    param.id = j.value("id", "");
    param.label = j.value("label", "");
    param.kind = interactiveParamKindFromString(j.value("kind", "text"));
    param.defaultValue = j.value("defaultValue", "");
    param.minValue = j.value("minValue", 0.0);
    param.maxValue = j.value("maxValue", 100.0);
    param.step = j.value("step", 1.0);
    param.choices = j.value("choices", std::vector<std::string>{});
    param.history = j.value("history", std::vector<InteractiveParamHistory>{});
}

void to_json(json& j, const LaunchItem& item)
{
    j = json{{"id", item.id},
             {"name", item.name},
             {"subtitle", item.subtitle},
             {"target", item.target.string()},
             {"startDirectory", item.startDirectory.string()},
             {"arguments", item.arguments},
             {"icon", item.icon},
             {"fallbackColor", item.fallbackColor},
             {"keywords", item.keywords},
             {"hotkey", item.hotkey},
             {"remark", item.remark},
             {"type", typeToString(item.type)},
             {"runAsAdmin", item.runAsAdmin},
             {"hotkeyInputMode", item.hotkeyInputMode},
             {"priority", item.priority},
             {"titleSize", item.titleSize},
             {"titleAlign", item.titleAlign},
             {"lockLayout", item.lockLayout},
             {"runCount", item.runCount},
             {"createdAt", item.createdAt},
             {"lastEditedAt", item.lastEditedAt},
             {"lastRunAt", item.lastRunAt},
             {"interactive", item.interactive},
             {"interactiveParams", item.interactiveParams},
             {"children", item.children}};
}

void from_json(const json& j, LaunchItem& item)
{
    item.id = j.value("id", "");
    item.name = j.value("name", "");
    item.subtitle = j.value("subtitle", "");
    item.target = j.value("target", "");
    item.startDirectory = j.value("startDirectory", "");
    item.arguments = j.value("arguments", "");
    item.icon = j.value("icon", "");
    item.fallbackColor = j.value("fallbackColor", "#8C8C8CFF");
    item.keywords = j.value("keywords", "");
    item.hotkey = j.value("hotkey", "");
    item.remark = j.value("remark", "");
    item.type = typeFromString(j.value("type", "app"));
    item.runAsAdmin = j.value("runAsAdmin", false);
    item.hotkeyInputMode = j.value("hotkeyInputMode", false);
    item.priority = j.value("priority", 2);
    item.titleSize = j.value("titleSize", 18);
    item.titleAlign = j.value("titleAlign", 0);
    item.lockLayout = j.value("lockLayout", false);
    item.runCount = j.value("runCount", 0);
    item.createdAt = j.value("createdAt", 0LL);
    item.lastEditedAt = j.value("lastEditedAt", 0LL);
    item.lastRunAt = j.value("lastRunAt", 0LL);
    item.interactive = j.value("interactive", false);
    item.interactiveParams = j.value("interactiveParams", std::vector<InteractiveParam>{});
    item.children = j.value("children", std::vector<LaunchItem>{});
}

void to_json(json& j, const Category& category)
{
    j = json{{"id", category.id},
             {"name", category.name},
             {"iconName", category.iconName},
             {"iconColor", category.iconColor},
             {"useGlobalLayout", category.useGlobalLayout},
             {"iconSize", category.iconSize},
             {"viewMode", viewModeToString(category.viewMode)},
             {"nameLines", category.nameLines},
             {"lockLayout", category.lockLayout},
             {"items", category.items}};
}

void from_json(const json& j, Category& category)
{
    category.id = j.value("id", "");
    category.name = j.value("name", "");
    category.iconName = j.value("iconName", j.value("icon", ""));
    category.iconColor = j.value("iconColor", "#FFFFFFFF");
    category.useGlobalLayout = j.value("useGlobalLayout", true);
    category.iconSize = std::clamp(j.value("iconSize", 48), 24, 96);
    category.viewMode = viewModeFromString(j.value("viewMode", "icon"));
    category.nameLines = std::clamp(j.value("nameLines", 1), 0, 3);
    category.lockLayout = j.value("lockLayout", false);
    category.items = j.value("items", std::vector<LaunchItem>{});
}

void to_json(json& j, const PluginPreference& plugin)
{
    j = json{{"id", plugin.id}, {"enabled", plugin.enabled}, {"settings", plugin.settings}};
}

void from_json(const json& j, PluginPreference& plugin)
{
    plugin.id = j.value("id", "");
    plugin.enabled = j.value("enabled", false);
    plugin.settings = j.value("settings", std::map<std::string, std::string>{});
}

void to_json(json& j, const AppSettings& settings)
{
    j = json{{"language", settings.language},
             {"startHidden", settings.startHidden},
             {"alwaysOnTop", settings.alwaysOnTop},
             {"lockLayout", settings.lockLayout},
             {"lockWindowPosition", settings.lockWindowPosition},
             {"lockWindowSize", settings.lockWindowSize},
             {"lockItemLayout", settings.lockItemLayout},
             {"dragBlankAreaMoveWindow", settings.dragBlankAreaMoveWindow},
             {"enableDocking", settings.enableDocking},
             {"enableViewport", settings.enableViewport},
             {"startWithWindows", settings.startWithWindows},
             {"minimizeToTray", settings.minimizeToTray},
             {"showTaskbarIcon", settings.showTaskbarIcon},
             {"fullscreenDoNotDisturb", settings.fullscreenDoNotDisturb},
             {"enableGlobalHotkey", settings.enableGlobalHotkey},
             {"globalHotkey", settings.globalHotkey},
             {"enableSearchHotkey", settings.enableSearchHotkey},
             {"searchHotkey", settings.searchHotkey},
             {"themeId", settings.themeId},
             {"showSearchButton", settings.showSearchButton},
             {"showMenuButton", settings.showMenuButton},
             {"showCloseButton", settings.showCloseButton},
             {"iconSize", settings.iconSize},
             {"useDefaultIcons", settings.useDefaultIcons},
             {"searchDelayMs", settings.searchDelayMs},
             {"viewMode", viewModeToString(settings.viewMode)},
             {"sortMode", sortModeToString(settings.sortMode)},
             {"nameLines", settings.nameLines},
             {"tooltipEnabled", settings.tooltipEnabled},
             {"tooltipFollowMouse", settings.tooltipFollowMouse},
             {"tooltipRunCount", settings.tooltipRunCount},
             {"tooltipTarget", settings.tooltipTarget},
             {"tooltipArguments", settings.tooltipArguments},
             {"tooltipRemark", settings.tooltipRemark},
             {"tooltipCreatedAt", settings.tooltipCreatedAt},
             {"tooltipLastEditedAt", settings.tooltipLastEditedAt},
             {"tooltipLastRunAt", settings.tooltipLastRunAt},
             {"itemTooltipOpacity", settings.itemTooltipOpacity},
             {"showMenuShortcutHints", settings.showMenuShortcutHints},
             {"runItemHidesMain", settings.runItemHidesMain},
             {"closeSearchAfterRun", settings.closeSearchAfterRun},
             {"hideSearchAfterMainClose", settings.hideSearchAfterMainClose},
             {"clearSelectionAfterMainClose", settings.clearSelectionAfterMainClose},
             {"searchAltNumberRun", settings.searchAltNumberRun},
             {"smoothInput", settings.smoothInput},
             {"autoUpdateEnvironment", settings.autoUpdateEnvironment},
             {"autoCheckUpdates", settings.autoCheckUpdates},
             {"checkUpdatesAtStartup", settings.checkUpdatesAtStartup},
             {"checkUpdatesDaily", settings.checkUpdatesDaily},
             {"lastUpdateCheckAt", settings.lastUpdateCheckAt},
             {"dragEnhanced", settings.dragEnhanced},
             {"dragSwapPlaceholder", settings.dragSwapPlaceholder},
             {"doubleClickRun", settings.doubleClickRun},
             {"keepScrollAfterWake", settings.keepScrollAfterWake},
             {"middleClickRunsCategory", settings.middleClickRunsCategory},
             {"virtualFolderRunsAll", settings.virtualFolderRunsAll},
             {"shiftRightClickExplorerMenu", settings.shiftRightClickExplorerMenu},
             {"directorySearchContextMenu", settings.directorySearchContextMenu},
             {"wakeFollowMouse", settings.wakeFollowMouse},
             {"magneticScreenCorner", settings.magneticScreenCorner},
             {"magneticScreenEdge", settings.magneticScreenEdge},
             {"wakeByTrayHover", settings.wakeByTrayHover},
             {"hideOnMouseLeave", settings.hideOnMouseLeave},
             {"hideOnFocusLost", settings.hideOnFocusLost},
             {"hideOnBlankDoubleClick", settings.hideOnBlankDoubleClick},
             {"hideAfterRun", settings.hideAfterRun},
             {"searchRegex", settings.searchRegex},
             {"searchPinyinInitial", settings.searchPinyinInitial},
             {"searchPinyin", settings.searchPinyin},
             {"searchEnglishMode", settings.searchEnglishMode},
             {"searchScopeTarget", settings.searchScopeTarget},
             {"searchScopeRemark", settings.searchScopeRemark},
             {"searchParamVariable", settings.searchParamVariable},
             {"advancedSearch", settings.advancedSearch},
             {"enableGlobalSearch", settings.enableGlobalSearch},
             {"globalSearchHideSystemPaths", settings.globalSearchHideSystemPaths},
             {"globalSearchScanIntensity", settings.globalSearchScanIntensity},
             {"searchResultLimit", settings.searchResultLimit},
             {"globalSearchResultLimit", settings.globalSearchResultLimit},
             {"enableAnimations", settings.enableAnimations},
             {"animationSpeedPercent", settings.animationSpeedPercent},
             {"plugins", settings.plugins}};
}

void from_json(const json& j, AppSettings& settings)
{
    settings.language = j.value("language", "zh-CN");
    settings.startHidden = j.value("startHidden", false);
    settings.alwaysOnTop = j.value("alwaysOnTop", false);
    settings.lockLayout = j.value("lockLayout", false);
    settings.lockWindowPosition = j.value("lockWindowPosition", settings.lockLayout);
    settings.lockWindowSize = j.value("lockWindowSize", false);
    settings.lockItemLayout = j.value("lockItemLayout", settings.lockLayout);
    settings.dragBlankAreaMoveWindow = j.value("dragBlankAreaMoveWindow", false);
    settings.enableDocking = j.value("enableDocking", false);
    settings.enableViewport = j.value("enableViewport", true);
    settings.startWithWindows = j.value("startWithWindows", false);
    settings.minimizeToTray = j.value("minimizeToTray", true);
    settings.showTaskbarIcon = j.value("showTaskbarIcon", false);
    settings.fullscreenDoNotDisturb = j.value("fullscreenDoNotDisturb", true);
    settings.enableGlobalHotkey = j.value("enableGlobalHotkey", true);
    settings.globalHotkey = j.value("globalHotkey", "Ctrl+Alt+Space");
    settings.enableSearchHotkey = j.value("enableSearchHotkey", false);
    settings.searchHotkey = j.value("searchHotkey", "Ctrl+Alt+F");
    settings.themeId = j.value("themeId", "");
    if (settings.themeId.empty()) {
        settings.themeId = j.value("themeMode", 0) == 1 ? "builtin:dark" : "builtin:light";
    }
    settings.showSearchButton = j.value("showSearchButton", true);
    settings.showMenuButton = j.value("showMenuButton", true);
    settings.showCloseButton = j.value("showCloseButton", true);
    settings.iconSize = j.value("iconSize", 48);
    settings.useDefaultIcons = j.value("useDefaultIcons", false);
    settings.searchDelayMs = j.value("searchDelayMs", 180);
    settings.viewMode = viewModeFromString(j.value("viewMode", "icon"));
    settings.sortMode = sortModeFromString(j.value("sortMode", "free"));
    settings.nameLines = j.value("nameLines", 1);
    settings.tooltipEnabled = j.value("tooltipEnabled", true);
    settings.tooltipFollowMouse = j.value("tooltipFollowMouse", false);
    settings.tooltipRunCount = j.value("tooltipRunCount", true);
    settings.tooltipTarget = j.value("tooltipTarget", true);
    settings.tooltipArguments = j.value("tooltipArguments", true);
    settings.tooltipRemark = j.value("tooltipRemark", true);
    settings.tooltipCreatedAt = j.value("tooltipCreatedAt", false);
    settings.tooltipLastEditedAt = j.value("tooltipLastEditedAt", false);
    settings.tooltipLastRunAt = j.value("tooltipLastRunAt", false);
    settings.itemTooltipOpacity = std::clamp(j.value("itemTooltipOpacity", 100), 0, 100);
    settings.showMenuShortcutHints = j.value("showMenuShortcutHints", true);
    settings.runItemHidesMain = j.value("runItemHidesMain", false);
    settings.closeSearchAfterRun = j.value("closeSearchAfterRun", true);
    settings.hideSearchAfterMainClose = j.value("hideSearchAfterMainClose", true);
    settings.clearSelectionAfterMainClose = j.value("clearSelectionAfterMainClose", true);
    settings.searchAltNumberRun = j.value("searchAltNumberRun", true);
    settings.smoothInput = j.value("smoothInput", true);
    settings.autoUpdateEnvironment = j.value("autoUpdateEnvironment", true);
    settings.autoCheckUpdates = j.value("autoCheckUpdates", true);
    settings.checkUpdatesAtStartup = j.value("checkUpdatesAtStartup", true);
    settings.checkUpdatesDaily = j.value("checkUpdatesDaily", true);
    settings.lastUpdateCheckAt = j.value("lastUpdateCheckAt", static_cast<std::int64_t>(0));
    settings.dragEnhanced = j.value("dragEnhanced", true);
    settings.dragSwapPlaceholder = j.value("dragSwapPlaceholder", true);
    settings.doubleClickRun = j.value("doubleClickRun", true);
    settings.keepScrollAfterWake = j.value("keepScrollAfterWake", true);
    settings.middleClickRunsCategory = j.value("middleClickRunsCategory", true);
    settings.virtualFolderRunsAll = j.value("virtualFolderRunsAll", true);
    settings.shiftRightClickExplorerMenu = j.value("shiftRightClickExplorerMenu", true);
    settings.directorySearchContextMenu = j.value("directorySearchContextMenu", true);
    settings.wakeFollowMouse = j.value("wakeFollowMouse", false);
    settings.magneticScreenCorner = j.value("magneticScreenCorner", false);
    settings.magneticScreenEdge = j.value("magneticScreenEdge", false);
    settings.wakeByTrayHover = j.value("wakeByTrayHover", true);
    settings.hideOnMouseLeave = j.value("hideOnMouseLeave", false);
    settings.hideOnFocusLost = j.value("hideOnFocusLost", false);
    settings.hideOnBlankDoubleClick = j.value("hideOnBlankDoubleClick", false);
    settings.hideAfterRun = j.value("hideAfterRun", true);
    settings.searchRegex = j.value("searchRegex", true);
    settings.searchPinyinInitial = j.value("searchPinyinInitial", true);
    settings.searchPinyin = j.value("searchPinyin", true);
    settings.searchEnglishMode = j.value("searchEnglishMode", false);
    settings.searchScopeTarget = j.value("searchScopeTarget", false);
    settings.searchScopeRemark = j.value("searchScopeRemark", false);
    settings.searchParamVariable = j.value("searchParamVariable", true);
    settings.advancedSearch = j.value("advancedSearch", false);
    settings.enableGlobalSearch = j.value("enableGlobalSearch", false);
    settings.globalSearchHideSystemPaths = j.value("globalSearchHideSystemPaths", true);
    settings.globalSearchScanIntensity = std::clamp(j.value("globalSearchScanIntensity", 1), 0, 2);
    settings.searchResultLimit = std::clamp(j.value("searchResultLimit", 128), 20, 512);
    settings.globalSearchResultLimit = std::clamp(j.value("globalSearchResultLimit", 80), 0, 512);
    settings.enableAnimations = j.value("enableAnimations", true);
    settings.animationSpeedPercent = std::clamp(j.value("animationSpeedPercent", 100), 25, 250);
    settings.plugins = j.value("plugins", std::vector<PluginPreference>{});
}

ConfigStore::ConfigStore()
    : defaultConfigPath_(defaultConfigPath())
    , locationPath_(defaultConfigPath_.parent_path() / kConfigLocationFileName)
    , configPath_(configuredConfigPath(defaultConfigPath_, locationPath_))
{
    migrateLegacyConfigIfNeeded(configPath_);
}

ConfigStore::ConfigStore(std::filesystem::path configPath)
    : defaultConfigPath_(std::move(configPath))
    , locationPath_(defaultConfigPath_.parent_path() / kConfigLocationFileName)
    , configPath_(defaultConfigPath_)
{}

std::optional<PersistedState> ConfigStore::tryLoadPersisted(std::string* error) const
{
    PersistedState state;
    std::ifstream input(configPath_);
    if (!input) {
        return state;
    }

    try {
        json j;
        input >> j;
        if (!j.is_object()) {
            if (error) {
                *error = "config root must be a JSON object";
            }
            return std::nullopt;
        }
        migrateConfig(j);
        state.settings = j.value("settings", AppSettings{});
        state.categories = j.value("categories", std::vector<Category>{});
        state.scheduledTasks = j.value("scheduledTasks", std::vector<ScheduledTask>{});
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        return std::nullopt;
    }
    return state;
}

PersistedState ConfigStore::loadPersisted() const
{
    return tryLoadPersisted().value_or(PersistedState{});
}

AppState ConfigStore::load() const
{
    AppState state;
    state.persisted() = loadPersisted();
    return state;
}

void ConfigStore::save(const PersistedState& state) const
{
    std::filesystem::create_directories(configPath_.parent_path());

    json j;
    j[kSchemaVersionKey] = kCurrentConfigSchemaVersion;
    j["settings"] = state.settings;
    j["categories"] = state.categories;
    j["scheduledTasks"] = state.scheduledTasks;
    const std::string content = j.dump(2);

    // Atomic write: write to temp file, then rename
    const std::filesystem::path tmpPath = configPath_.parent_path() / "config.json.tmp";
    {
        std::ofstream output(tmpPath, std::ios::binary);
        if (!output) {
            return;
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!output) {
            std::error_code ec;
            std::filesystem::remove(tmpPath, ec);
            return;
        }
    }

    if (!MoveFileExW(tmpPath.wstring().c_str(), configPath_.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        std::filesystem::remove(tmpPath, ec);
    }
}

void ConfigStore::save(const AppState& state) const
{
    save(state.persisted());
}

bool ConfigStore::moveConfigDirectory(const std::filesystem::path& directory, std::string* error)
{
    if (directory.empty()) {
        setError(error, "config directory is empty");
        return false;
    }

    std::error_code ec;
    const std::filesystem::path targetDirectory = std::filesystem::absolute(directory, ec).lexically_normal();
    if (ec) {
        setError(error, ec.message());
        return false;
    }
    const std::filesystem::path targetPath = targetDirectory / kConfigFileName;

    std::filesystem::create_directories(targetDirectory, ec);
    if (ec) {
        setError(error, ec.message());
        return false;
    }

    if (!samePath(configPath_, targetPath)) {
        const bool hasSourceConfig = std::filesystem::exists(configPath_, ec);
        if (ec) {
            setError(error, ec.message());
            return false;
        }
        if (hasSourceConfig) {
            std::filesystem::copy_file(configPath_, targetPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                setError(error, ec.message());
                return false;
            }
        }
        const std::filesystem::path sourceNotes = configPath_.parent_path() / "notes";
        const std::filesystem::path targetNotes = targetDirectory / "notes";
        if (std::filesystem::exists(sourceNotes, ec) && !samePath(sourceNotes, targetNotes)) {
            std::filesystem::create_directories(targetNotes, ec);
            if (ec) {
                setError(error, ec.message());
                return false;
            }
            std::filesystem::copy(sourceNotes, targetNotes,
                                  std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                setError(error, ec.message());
                return false;
            }
        }
    }

    if (!writeConfigLocation(defaultConfigPath_, locationPath_, targetDirectory, error)) {
        return false;
    }

    configPath_ = targetPath;
    return true;
}

} // namespace launcher

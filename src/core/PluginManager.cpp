#include "core/PluginManager.hpp"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <system_error>
#include <thread>

namespace launcher {
namespace {

using json = nlohmann::json;

constexpr DWORD kPluginRequestTimeoutMs = 2000;
constexpr DWORD kPluginSearchTimeoutMs = 180;
constexpr int kNativePluginAbiVersion = 1;
constexpr int kNativePluginResponseBytes = 1024 * 1024;

using NativeRequestFn = int(__cdecl*)(const char* requestJson, char* responseJson, int responseCapacity);
using NativeShutdownFn = void(__cdecl*)();
using NativeAbiVersionFn = int(__cdecl*)();

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::vector<std::string> stringVector(const json& value)
{
    std::vector<std::string> result;
    if (!value.is_array()) {
        return result;
    }
    for (const json& item : value) {
        if (item.is_string()) {
            result.push_back(item.get<std::string>());
        }
    }
    return result;
}

bool hasCapability(const PluginInfo& plugin, const std::string& capability)
{
    return std::find(plugin.capabilities.begin(), plugin.capabilities.end(), capability) != plugin.capabilities.end();
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

PluginPreference& preferenceFor(AppSettings& settings, const std::string& id, bool* created = nullptr)
{
    auto it = std::find_if(settings.plugins.begin(), settings.plugins.end(), [&](const PluginPreference& pref) {
        return pref.id == id;
    });
    if (it == settings.plugins.end()) {
        PluginPreference pref;
        pref.id = id;
        settings.plugins.push_back(std::move(pref));
        if (created) {
            *created = true;
        }
        return settings.plugins.back();
    }
    if (created) {
        *created = false;
    }
    return *it;
}

const PluginPreference* preferenceFor(const AppSettings& settings, const std::string& id)
{
    auto it = std::find_if(settings.plugins.begin(), settings.plugins.end(), [&](const PluginPreference& pref) {
        return pref.id == id;
    });
    return it == settings.plugins.end() ? nullptr : &*it;
}

std::wstring quoteCommandPath(const std::filesystem::path& path)
{
    std::wstring text = path.wstring();
    std::wstring result = L"\"";
    for (wchar_t ch : text) {
        if (ch == L'"') {
            result += L"\\\"";
        } else {
            result.push_back(ch);
        }
    }
    result += L"\"";
    return result;
}

json settingsJson(const std::map<std::string, std::string>& settings)
{
    json result = json::object();
    for (const auto& [key, value] : settings) {
        result[key] = value;
    }
    return result;
}

json taskJson(const ScheduledTask& task)
{
    return json{{"id", task.id},
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
                {"runMinimized", task.runMinimized},
                {"retryCount", task.retryCount},
                {"retryDelaySeconds", task.retryDelaySeconds},
                {"lastRunAt", task.lastRunAt},
                {"nextRunAt", task.nextRunAt},
                {"lastSuccess", task.lastSuccess},
                {"lastMessage", task.lastMessage}};
}

json tasksJson(const std::vector<ScheduledTask>& tasks)
{
    json result = json::array();
    for (const ScheduledTask& task : tasks) {
        result.push_back(taskJson(task));
    }
    return result;
}

ScheduledTask taskFromJson(const json& value)
{
    ScheduledTask task;
    if (!value.is_object()) {
        return task;
    }
    task.id = value.value("id", "");
    task.name = value.value("name", "");
    task.enabled = value.value("enabled", true);
    task.trigger = scheduledTriggerKindFromString(value.value("trigger", "daily"));
    task.action = scheduledActionKindFromString(value.value("action", "launch-item"));
    task.itemId = value.value("itemId", "");
    task.hour = std::clamp(value.value("hour", 9), 0, 23);
    task.minute = std::clamp(value.value("minute", 0), 0, 59);
    task.weekdayMask = std::clamp(value.value("weekdayMask", 0x7F), 0, 0x7F);
    task.intervalMinutes = std::max(1, value.value("intervalMinutes", 60));
    task.onceAt = value.value("onceAt", 0LL);
    task.processName = value.value("processName", "");
    task.runMissed = value.value("runMissed", false);
    task.runMinimized = value.value("runMinimized", false);
    task.retryCount = std::clamp(value.value("retryCount", 0), 0, 10);
    task.retryDelaySeconds = std::clamp(value.value("retryDelaySeconds", 60), 1, 3600);
    return task;
}

void addTaskContext(json& params, const PluginInfo& plugin, const std::vector<ScheduledTask>& tasks)
{
    if (hasCapability(plugin, "tasks")) {
        params["tasks"] = tasksJson(tasks);
    }
}

void ensurePluginCacheDirectory(const json& request)
{
    if (!request.is_object()) {
        return;
    }
    const auto params = request.find("params");
    if (params == request.end() || !params->is_object()) {
        return;
    }
    const std::string cacheDir = params->value("cacheDir", "");
    if (cacheDir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);
}

bool runProcessPluginRequest(const PluginInfo& plugin, const json& request, DWORD timeoutMs, json* response)
{
    const std::filesystem::path exe = plugin.directory / plugin.entry;
    ensurePluginCacheDirectory(request);
    std::wstring command = quoteCommandPath(exe);

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE childStdInRead = nullptr;
    HANDLE childStdInWrite = nullptr;
    HANDLE childStdOutRead = nullptr;
    HANDLE childStdOutWrite = nullptr;
    if (!CreatePipe(&childStdInRead, &childStdInWrite, &security, 0)) {
        return false;
    }
    if (!SetHandleInformation(childStdInWrite, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(childStdInRead);
        CloseHandle(childStdInWrite);
        return false;
    }
    if (!CreatePipe(&childStdOutRead, &childStdOutWrite, &security, 0)) {
        CloseHandle(childStdInRead);
        CloseHandle(childStdInWrite);
        return false;
    }
    if (!SetHandleInformation(childStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(childStdInRead);
        CloseHandle(childStdInWrite);
        CloseHandle(childStdOutRead);
        CloseHandle(childStdOutWrite);
        return false;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childStdInRead;
    startup.hStdOutput = childStdOutWrite;
    startup.hStdError = childStdOutWrite;

    PROCESS_INFORMATION process{};
    std::wstring workingDirectory = plugin.directory.wstring();
    const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process);
    CloseHandle(childStdInRead);
    CloseHandle(childStdOutWrite);
    if (!created) {
        CloseHandle(childStdInWrite);
        CloseHandle(childStdOutRead);
        return false;
    }

    std::string line = request.dump();
    line.push_back('\n');
    DWORD written = 0;
    WriteFile(childStdInWrite, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(childStdInWrite);

    std::string output;
    const auto start = std::chrono::steady_clock::now();
    bool timedOut = false;
    while (true) {
        DWORD available = 0;
        if (PeekNamedPipe(childStdOutRead, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            char buffer[512]{};
            DWORD read = 0;
            if (ReadFile(childStdOutRead, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr) && read > 0) {
                output.append(buffer, buffer + read);
                if (output.find('\n') != std::string::npos) {
                    break;
                }
            }
        }
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) {
            if (available == 0) {
                break;
            }
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeoutMs) {
            timedOut = true;
            TerminateProcess(process.hProcess, 1);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }

    CloseHandle(childStdOutRead);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (timedOut || output.empty()) {
        return false;
    }
    const size_t newline = output.find('\n');
    if (newline != std::string::npos) {
        output.resize(newline);
    }
    try {
        if (response) {
            *response = json::parse(output);
        }
        return true;
    } catch (...) {
        return false;
    }
}

PluginAction parseAction(const json& value)
{
    PluginAction action;
    if (value.is_string()) {
        action.id = value.get<std::string>();
        action.label = action.id;
    } else if (value.is_object()) {
        action.id = value.value("id", "");
        action.label = value.value("label", action.id);
    }
    return action;
}

std::vector<PluginAction> parseActions(const json& value)
{
    std::vector<PluginAction> result;
    if (!value.is_array()) {
        return result;
    }
    for (const json& node : value) {
        PluginAction action = parseAction(node);
        if (!action.id.empty()) {
            result.push_back(std::move(action));
        }
    }
    return result;
}

PluginTaskOperation parseTaskOperation(const json& value)
{
    PluginTaskOperation operation;
    if (!value.is_object()) {
        return operation;
    }
    const std::string op = value.value("op", value.value("operation", ""));
    if (op == "task.update" || op == "update") {
        operation.kind = PluginTaskOperationKind::Update;
    } else if (op == "task.delete" || op == "delete") {
        operation.kind = PluginTaskOperationKind::Delete;
    } else if (op == "task.run" || op == "run") {
        operation.kind = PluginTaskOperationKind::Run;
    } else if (op == "task.enable" || op == "task.disable" || op == "set-enabled" || op == "enable" || op == "disable") {
        operation.kind = PluginTaskOperationKind::SetEnabled;
        operation.enabled = op != "task.disable" && op != "disable";
    } else {
        operation.kind = PluginTaskOperationKind::Create;
    }
    operation.taskId = value.value("taskId", value.value("id", ""));
    if (const auto enabled = value.find("enabled"); enabled != value.end() && enabled->is_boolean()) {
        operation.enabled = enabled->get<bool>();
    }
    if (const auto task = value.find("task"); task != value.end()) {
        operation.task = taskFromJson(*task);
        if (operation.taskId.empty()) {
            operation.taskId = operation.task.id;
        }
    } else {
        operation.task = taskFromJson(value);
    }
    return operation;
}

std::vector<PluginTaskOperation> parseTaskOperations(const json& value)
{
    std::vector<PluginTaskOperation> result;
    if (!value.is_array()) {
        return result;
    }
    for (const json& node : value) {
        PluginTaskOperation operation = parseTaskOperation(node);
        if (operation.kind == PluginTaskOperationKind::Create || !operation.taskId.empty() || !operation.task.id.empty()) {
            result.push_back(std::move(operation));
        }
    }
    return result;
}

std::vector<PluginTaskOperation> parseTaskOperationsFromResponse(const json& response)
{
    const json payload = response.contains("result") ? response["result"] : response;
    if (!payload.is_object()) {
        return {};
    }
    if (const auto operations = payload.find("taskOperations"); operations != payload.end()) {
        return parseTaskOperations(*operations);
    }
    if (const auto operations = payload.find("tasks"); operations != payload.end()) {
        return parseTaskOperations(*operations);
    }
    return {};
}

} // namespace

PluginManager::~PluginManager()
{
    unloadNativeModules();
}

void PluginManager::unloadNativeModules()
{
    for (auto& [id, native] : nativeModules_) {
        if (native.shutdown) {
            reinterpret_cast<NativeShutdownFn>(native.shutdown)();
        }
        if (native.module) {
            FreeLibrary(static_cast<HMODULE>(native.module));
        }
    }
    nativeModules_.clear();
}

bool PluginManager::runNativePluginRequest(const PluginInfo& plugin, const std::string& requestText, std::string* responseText) const
{
    std::lock_guard lock(mutex_);
    NativeModule& native = nativeModules_[plugin.id];
    if (!native.module) {
        const std::filesystem::path dllPath = plugin.directory / plugin.entry;
        HMODULE module = LoadLibraryW(dllPath.wstring().c_str());
        if (!module) {
            nativeModules_.erase(plugin.id);
            return false;
        }
        auto* versionFn = reinterpret_cast<NativeAbiVersionFn>(GetProcAddress(module, "launcher_plugin_abi_version"));
        if (versionFn && versionFn() != kNativePluginAbiVersion) {
            FreeLibrary(module);
            nativeModules_.erase(plugin.id);
            return false;
        }
        auto* requestFn = reinterpret_cast<NativeRequestFn>(GetProcAddress(module, "launcher_plugin_request"));
        if (!requestFn) {
            FreeLibrary(module);
            nativeModules_.erase(plugin.id);
            return false;
        }
        native.module = module;
        native.request = reinterpret_cast<void*>(requestFn);
        native.shutdown = reinterpret_cast<void*>(GetProcAddress(module, "launcher_plugin_shutdown"));
    }

    auto* requestFn = reinterpret_cast<NativeRequestFn>(native.request);
    std::vector<char> output(static_cast<size_t>(kNativePluginResponseBytes), '\0');
    const int ok = requestFn(requestText.c_str(), output.data(), static_cast<int>(output.size()));
    if (ok == 0) {
        return false;
    }
    const auto end = std::find(output.begin(), output.end(), '\0');
    if (end == output.begin()) {
        return false;
    }
    if (responseText) {
        *responseText = std::string(output.begin(), end);
    }
    return true;
}

void PluginManager::load(const std::vector<std::filesystem::path>& roots, const std::filesystem::path& stateDirectory,
                         AppSettings& settings)
{
    std::lock_guard lock(mutex_);
    unloadNativeModules();
    plugins_.clear();
    stateDirectory_ = stateDirectory;
    std::set<std::string> seen;

    for (const std::filesystem::path& root : roots) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (!entry.is_directory(ec)) {
                continue;
            }
            PluginInfo plugin;
            plugin.directory = entry.path();
            try {
                const std::filesystem::path manifestPath = entry.path() / "plugin.json";
                if (!std::filesystem::exists(manifestPath, ec)) {
                    continue;
                }
                const json manifest = json::parse(readTextFile(manifestPath));
                plugin.id = manifest.value("id", "");
                if (plugin.id.empty() || seen.contains(plugin.id)) {
                    continue;
                }
                plugin.name = manifest.value("name", plugin.id);
                plugin.version = manifest.value("version", "");
                plugin.author = manifest.value("author", "");
                plugin.description = manifest.value("description", "");
                plugin.entry = manifest.value("entry", "");
                plugin.type = manifest.value("type", "process");
                plugin.lifecycle = manifest.value("lifecycle", "on-demand");
                plugin.capabilities = stringVector(manifest.value("capabilities", json::array()));
                plugin.defaultEnabled = manifest.value("enabled", false);
                if (const auto schema = manifest.find("settings"); schema != manifest.end() && schema->is_array()) {
                    for (const json& node : *schema) {
                        PluginSettingField field;
                        field.id = node.value("id", "");
                        field.label = node.value("label", field.id);
                        field.type = node.value("type", "text");
                        field.defaultValue = node.value("default", "");
                        field.choices = stringVector(node.value("choices", json::array()));
                        if (!field.id.empty()) {
                            plugin.settingsSchema.push_back(std::move(field));
                        }
                    }
                }
                if (plugin.entry.empty()) {
                    plugin.loadError = "missing entry";
                } else if (plugin.type != "process" && plugin.type != "native") {
                    plugin.loadError = "unsupported plugin type";
                }
                bool createdPreference = false;
                PluginPreference& pref = preferenceFor(settings, plugin.id, &createdPreference);
                for (const PluginSettingField& field : plugin.settingsSchema) {
                    if (!field.defaultValue.empty() && !pref.settings.contains(field.id)) {
                        pref.settings[field.id] = field.defaultValue;
                    }
                }
                if (createdPreference && plugin.defaultEnabled) {
                    pref.enabled = true;
                }
                plugin.enabled = pref.enabled;
                seen.insert(plugin.id);
                plugins_.push_back(std::move(plugin));
            } catch (const std::exception& ex) {
                plugin.id = entry.path().filename().string();
                plugin.name = plugin.id;
                plugin.loadError = ex.what();
                plugins_.push_back(std::move(plugin));
            }
        }
    }
    std::sort(plugins_.begin(), plugins_.end(), [](const PluginInfo& a, const PluginInfo& b) {
        return a.name < b.name;
    });
}

const std::vector<PluginInfo>& PluginManager::plugins() const
{
    return plugins_;
}

bool PluginManager::setEnabled(AppSettings& settings, const std::string& id, bool enabled)
{
    std::lock_guard lock(mutex_);
    PluginPreference& pref = preferenceFor(settings, id);
    pref.enabled = enabled;
    for (PluginInfo& plugin : plugins_) {
        if (plugin.id == id) {
            plugin.enabled = enabled;
            return true;
        }
    }
    return false;
}

std::map<std::string, std::string>& PluginManager::settingsFor(AppSettings& settings, const std::string& id)
{
    return preferenceFor(settings, id).settings;
}

std::filesystem::path PluginManager::pluginCacheDirectory(const std::string& id) const
{
    return stateDirectory_ / id;
}

const PluginInfo* PluginManager::findPlugin(const std::string& id) const
{
    auto it = std::find_if(plugins_.begin(), plugins_.end(), [&](const PluginInfo& plugin) {
        return plugin.id == id;
    });
    return it == plugins_.end() ? nullptr : &*it;
}

std::vector<PluginSearchResult> PluginManager::query(const AppSettings& settings, const std::vector<ScheduledTask>& tasks,
                                                     const std::string& queryText, int limit) const
{
    std::vector<PluginSearchResult> results;
    if (queryText.empty()) {
        return results;
    }
    std::vector<PluginInfo> plugins;
    {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            return results;
        }
        plugins = plugins_;
    }
    for (const PluginInfo& plugin : plugins) {
        const PluginPreference* pref = preferenceFor(settings, plugin.id);
        if (!plugin.enabled || !pref || !pref->enabled || !plugin.loadError.empty() || !hasCapability(plugin, "search")) {
            continue;
        }
        json response;
        json params{{"query", queryText},
                    {"limit", limit},
                    {"settings", settingsJson(pref->settings)},
                    {"pluginDir", plugin.directory.string()},
                    {"cacheDir", pluginCacheDirectory(plugin.id).string()}};
        addTaskContext(params, plugin, tasks);
        json request{{"id", 1}, {"method", "search"}, {"params", std::move(params)}};
        ensurePluginCacheDirectory(request);
        if (plugin.type == "native") {
            std::string responseText;
            if (!runNativePluginRequest(plugin, request.dump(), &responseText)) {
                continue;
            }
            try {
                response = json::parse(responseText);
            } catch (...) {
                continue;
            }
        } else {
            if (!runProcessPluginRequest(plugin, request, kPluginSearchTimeoutMs, &response)) {
                continue;
            }
        }
        json payload = response.contains("result") ? response["result"] : response;
        if (!payload.is_array()) {
            continue;
        }
        for (const json& node : payload) {
            if (!node.is_object()) {
                continue;
            }
            PluginSearchResult result;
            result.pluginId = plugin.id;
            result.id = node.value("id", "");
            result.title = node.value("title", "");
            result.subtitle = node.value("subtitle", "");
            result.icon = node.value("icon", "");
            result.score = node.value("score", 0);
            result.actions = parseActions(node.value("actions", json::array()));
            if (result.actions.empty()) {
                result.actions.push_back({"open", "Open"});
            }
            if (!result.id.empty() && !result.title.empty()) {
                results.push_back(std::move(result));
            }
        }
    }
    return results;
}

PluginRunResult PluginManager::runAction(const AppSettings& settings, const std::vector<ScheduledTask>& tasks,
                                         const PluginSearchResult& result, const std::string& actionId) const
{
    PluginRunResult runResult;
    PluginInfo pluginCopy;
    bool found = false;
    {
        std::lock_guard lock(mutex_);
        if (const PluginInfo* plugin = findPlugin(result.pluginId)) {
            pluginCopy = *plugin;
            found = true;
        }
    }
    const PluginPreference* pref = preferenceFor(settings, result.pluginId);
    if (!found || !pref || !pref->enabled || (!hasCapability(pluginCopy, "actions") && !hasCapability(pluginCopy, "search"))) {
        return runResult;
    }
    json response;
    json params{{"resultId", result.id},
                {"actionId", actionId.empty() ? "open" : actionId},
                {"settings", settingsJson(pref->settings)},
                {"pluginDir", pluginCopy.directory.string()},
                {"cacheDir", pluginCacheDirectory(pluginCopy.id).string()}};
    addTaskContext(params, pluginCopy, tasks);
    json request{{"id", 1}, {"method", "run"}, {"params", std::move(params)}};
    ensurePluginCacheDirectory(request);
    if (pluginCopy.type == "native") {
        std::string responseText;
        runResult.ok = runNativePluginRequest(pluginCopy, request.dump(), &responseText);
        if (runResult.ok) {
            try {
                response = json::parse(responseText);
            } catch (...) {
                runResult.ok = false;
            }
        }
    } else {
        runResult.ok = runProcessPluginRequest(pluginCopy, request, kPluginRequestTimeoutMs, &response);
    }
    if (runResult.ok) {
        const json payload = response.contains("result") ? response["result"] : response;
        if (payload.is_object()) {
            runResult.message = payload.value("message", "");
        }
        runResult.taskOperations = parseTaskOperationsFromResponse(response);
    }
    return runResult;
}

void PluginManager::notifyEvent(const AppSettings& settings, const std::vector<ScheduledTask>& tasks, const std::string& eventName,
                                const std::map<std::string, std::string>& fields) const
{
    std::vector<PluginInfo> plugins;
    {
        std::lock_guard lock(mutex_);
        plugins = plugins_;
    }
    for (const PluginInfo& plugin : plugins) {
        const PluginPreference* pref = preferenceFor(settings, plugin.id);
        if (!plugin.enabled || !pref || !pref->enabled || !hasCapability(plugin, "events")) {
            continue;
        }
        json payload = json::object();
        for (const auto& [key, value] : fields) {
            payload[key] = value;
        }
        json params{{"name", eventName},
                    {"fields", payload},
                    {"settings", settingsJson(pref->settings)},
                    {"pluginDir", plugin.directory.string()},
                    {"cacheDir", pluginCacheDirectory(plugin.id).string()}};
        addTaskContext(params, plugin, tasks);
        json request{{"id", 1}, {"method", "event"}, {"params", std::move(params)}};
        ensurePluginCacheDirectory(request);
        json ignored;
        if (plugin.type == "native") {
            std::string ignoredText;
            runNativePluginRequest(plugin, request.dump(), &ignoredText);
        } else {
            runProcessPluginRequest(plugin, request, 250, &ignored);
        }
    }
}

} // namespace launcher

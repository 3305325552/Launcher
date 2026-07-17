#pragma once

#include "launcher/Models.hpp"

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace launcher {

struct PluginAction {
    std::string id;
    std::string label;
};

struct PluginSettingField {
    std::string id;
    std::string label;
    std::string type = "text";
    std::string defaultValue;
    std::vector<std::string> choices;
};

struct PluginInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string entry;
    std::string type = "process";
    std::string lifecycle = "on-demand";
    std::filesystem::path directory;
    std::vector<std::string> capabilities;
    std::vector<PluginSettingField> settingsSchema;
    bool defaultEnabled = false;
    bool enabled = false;
    std::string loadError;
};

struct PluginSearchResult {
    std::string pluginId;
    std::string id;
    std::string title;
    std::string subtitle;
    std::string icon;
    int score = 0;
    std::vector<PluginAction> actions;
};

enum class PluginTaskOperationKind {
    Create,
    Update,
    Delete,
    Run,
    SetEnabled
};

struct PluginTaskOperation {
    PluginTaskOperationKind kind = PluginTaskOperationKind::Create;
    std::string taskId;
    ScheduledTask task;
    bool enabled = true;
};

struct PluginRunResult {
    bool ok = false;
    std::string message;
    std::vector<PluginTaskOperation> taskOperations;
};

class PluginManager {
public:
    ~PluginManager();

    void load(const std::vector<std::filesystem::path>& roots, const std::filesystem::path& stateDirectory, AppSettings& settings);
    const std::vector<PluginInfo>& plugins() const;
    bool setEnabled(AppSettings& settings, const std::string& id, bool enabled);
    std::map<std::string, std::string>& settingsFor(AppSettings& settings, const std::string& id);
    std::vector<PluginSearchResult> query(const AppSettings& settings, const std::vector<ScheduledTask>& tasks,
                                          const std::string& queryText, int limit) const;
    PluginRunResult runAction(const AppSettings& settings, const std::vector<ScheduledTask>& tasks, const PluginSearchResult& result,
                              const std::string& actionId) const;
    void notifyEvent(const AppSettings& settings, const std::vector<ScheduledTask>& tasks, const std::string& eventName,
                     const std::map<std::string, std::string>& fields) const;

private:
    struct NativeModule {
        void* module = nullptr;
        void* request = nullptr;
        void* shutdown = nullptr;
    };

    const PluginInfo* findPlugin(const std::string& id) const;
    std::filesystem::path pluginCacheDirectory(const std::string& id) const;
    void unloadNativeModules();
    bool runNativePluginRequest(const PluginInfo& plugin, const std::string& requestText, std::string* responseText) const;

    mutable std::mutex mutex_;
    std::vector<PluginInfo> plugins_;
    std::filesystem::path stateDirectory_;
    mutable std::unordered_map<std::string, NativeModule> nativeModules_;
};

} // namespace launcher

#include "app/AppContext.hpp"

#include "core/ModelActions.hpp"
#include "data/SeedData.hpp"

#include <windows.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <string>

namespace launcher {
namespace {

std::filesystem::path executableDirectory()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

std::int64_t nowUnix()
{
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::string makePluginTaskId()
{
    static int counter = 0;
    return "plugin-task-" + std::to_string(nowUnix()) + "-" + std::to_string(++counter);
}

ScheduledTask* findScheduledTask(std::vector<ScheduledTask>& tasks, const std::string& id)
{
    auto it = std::find_if(tasks.begin(), tasks.end(), [&](const ScheduledTask& task) {
        return task.id == id;
    });
    return it == tasks.end() ? nullptr : &*it;
}

void resetScheduledTaskRuntime(ScheduledTask& task)
{
    task.nextRunAt = 0;
    task.retryAt = 0;
    task.pendingRetries = 0;
}

bool runScheduledItem(AppContext& context, LaunchItem& item, int showCommand, std::string* message)
{
    if (item.type == LaunchItemType::Title || item.type == LaunchItemType::Placeholder) {
        if (message) *message = "item is not runnable";
        return false;
    }
    if (item.type == LaunchItemType::Note) {
        if (message) *message = "note items cannot run in background tasks";
        return false;
    }
    if (item.type == LaunchItemType::VirtualFolder) {
        bool any = false;
        bool all = true;
        for (LaunchItem& child : item.children) {
            if (child.type == LaunchItemType::VirtualFolder) {
                bool childOk = runScheduledItem(context, child, showCommand, message);
                any = any || childOk;
                all = all && childOk;
                continue;
            }
            if (child.type == LaunchItemType::Title || child.type == LaunchItemType::Placeholder || child.type == LaunchItemType::Note) {
                continue;
            }
            const bool ok = context.launcher.launch(child, showCommand);
            any = any || ok;
            all = all && ok;
            if (ok) {
                ++child.runCount;
                child.lastRunAt = nowUnix();
            }
        }
        if (message)
            *message = any ? (all ? "virtual folder completed" : "virtual folder completed with failures")
                           : "virtual folder has no runnable items";
        return any;
    }
    const bool ok = context.launcher.launch(item, showCommand);
    if (ok) {
        ++item.runCount;
        item.lastRunAt = nowUnix();
        if (message) *message = "completed";
    } else if (message) {
        *message = "launch failed";
    }
    return ok;
}

} // namespace

AppContext::~AppContext()
{
    stopGlobalSearch();
}

void AppContext::load()
{
    state = {};
    persisted() = config.loadPersisted();
    notes.setDirectory(config.directory() / "notes");
    notes.load();
    if (persisted().categories.empty()) {
        state = makeSeedState();
    }
    if (model_actions::ensureUniqueIds(persisted())) {
        config.save(persisted());
    }
    reloadPlugins();
    config.save(persisted());
    rebuildSearch();
    syncGlobalSearch();
    plugins.notifyEvent(persisted().settings, persisted().scheduledTasks, "app.started", {});
}

void AppContext::save()
{
    config.save(persisted());
}

std::vector<std::filesystem::path> AppContext::pluginRoots() const
{
    return {executableDirectory() / "plugins", config.directory() / "plugins"};
}

void AppContext::reloadPlugins()
{
    plugins.load(pluginRoots(), config.directory() / "plugin-cache", persisted().settings);
}

void AppContext::rebuildSearch()
{
    search.rebuild(persisted().categories, notes.notes());
}

void AppContext::commitContentChange()
{
    rebuildSearch();
    save();
    plugins.notifyEvent(persisted().settings, persisted().scheduledTasks, "content.changed", {});
}

LaunchItem* AppContext::findItemById(const std::string& id)
{
    for (Category& category : persisted().categories) {
        if (LaunchItem* item = model_actions::findItemInList(category.items, id)) {
            return item;
        }
    }
    return nullptr;
}

const LaunchItem* AppContext::findItemById(const std::string& id) const
{
    for (const Category& category : persisted().categories) {
        if (const LaunchItem* item = model_actions::findItemInList(category.items, id)) {
            return item;
        }
    }
    return nullptr;
}

bool AppContext::runScheduledTaskAction(const ScheduledTask& task, std::string* message)
{
    LaunchItem* item = findItemById(task.itemId);
    if (!item) {
        if (message) *message = "item not found";
        return false;
    }
    if (task.action == ScheduledActionKind::LaunchVirtualFolder && item->type != LaunchItemType::VirtualFolder) {
        if (message) *message = "selected item is not a virtual folder";
        return false;
    }
    const bool ok = runScheduledItem(*this, *item, task.runMinimized ? SW_SHOWMINIMIZED : SW_SHOWNORMAL, message);
    save();
    rebuildSearch();
    return ok;
}

bool AppContext::runPluginAction(const PluginSearchResult& result, const std::string& actionId, std::string* message)
{
    PluginRunResult runResult = plugins.runAction(persisted().settings, persisted().scheduledTasks, result, actionId);
    if (!runResult.ok) {
        if (message) *message = runResult.message.empty() ? "plugin action failed" : runResult.message;
        return false;
    }

    bool tasksChanged = false;
    bool taskRunFailed = false;
    for (PluginTaskOperation& operation : runResult.taskOperations) {
        std::vector<ScheduledTask>& tasks = persisted().scheduledTasks;
        const std::string taskId = operation.taskId.empty() ? operation.task.id : operation.taskId;
        switch (operation.kind) {
        case PluginTaskOperationKind::Create: {
            ScheduledTask task = operation.task;
            if (task.id.empty()) {
                task.id = makePluginTaskId();
            }
            if (task.name.empty()) {
                task.name = task.id;
            }
            resetScheduledTaskRuntime(task);
            tasks.push_back(std::move(task));
            tasksChanged = true;
            break;
        }
        case PluginTaskOperationKind::Update: {
            ScheduledTask* existing = findScheduledTask(tasks, taskId);
            if (!existing) {
                break;
            }
            const std::vector<ScheduledTaskHistory> history = existing->history;
            const std::int64_t lastRunAt = existing->lastRunAt;
            const bool lastSuccess = existing->lastSuccess;
            const std::string lastMessage = existing->lastMessage;
            ScheduledTask updated = operation.task;
            if (updated.id.empty()) {
                updated.id = existing->id;
            }
            updated.history = history;
            updated.lastRunAt = lastRunAt;
            updated.lastSuccess = lastSuccess;
            updated.lastMessage = lastMessage;
            resetScheduledTaskRuntime(updated);
            *existing = std::move(updated);
            tasksChanged = true;
            break;
        }
        case PluginTaskOperationKind::Delete: {
            const auto oldSize = tasks.size();
            tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                                       [&](const ScheduledTask& task) {
                                           return task.id == taskId;
                                       }),
                        tasks.end());
            tasksChanged = tasksChanged || tasks.size() != oldSize;
            break;
        }
        case PluginTaskOperationKind::SetEnabled: {
            if (ScheduledTask* task = findScheduledTask(tasks, taskId)) {
                task->enabled = operation.enabled;
                resetScheduledTaskRuntime(*task);
                tasksChanged = true;
            }
            break;
        }
        case PluginTaskOperationKind::Run: {
            if (ScheduledTask* task = findScheduledTask(tasks, taskId)) {
                std::string runMessage;
                const std::int64_t startedAt = nowUnix();
                const bool ok = runScheduledTaskAction(*task, &runMessage);
                const std::int64_t finishedAt = nowUnix();
                task->lastRunAt = startedAt;
                task->lastSuccess = ok;
                task->lastMessage = runMessage;
                task->history.insert(task->history.begin(), ScheduledTaskHistory{startedAt, finishedAt, ok, runMessage});
                if (task->history.size() > 20) {
                    task->history.resize(20);
                }
                taskRunFailed = taskRunFailed || !ok;
                tasksChanged = true;
            }
            break;
        }
        }
    }

    if (tasksChanged) {
        save();
        plugins.notifyEvent(persisted().settings, persisted().scheduledTasks, "tasks.changed", {});
    }
    if (message) {
        *message = runResult.message.empty() ? (taskRunFailed ? "plugin action completed with task failures" : "plugin action completed")
                                             : runResult.message;
    }
    return !taskRunFailed;
}

void AppContext::syncGlobalSearch()
{
    globalFiles.sync(persisted().settings.enableGlobalSearch, config.directory() / "global-search-cache.tsv",
                     persisted().settings.globalSearchScanIntensity,
                     [this](const std::string& rootId, std::vector<GlobalFileRecord> files, GlobalFileCommitMode mode) {
                         switch (mode) {
                         case GlobalFileCommitMode::ResetAll: search.rebuildGlobalFiles({}); break;
                         case GlobalFileCommitMode::ReplaceRoot: search.replaceGlobalRoot(rootId, std::move(files)); break;
                         case GlobalFileCommitMode::AppendRoot: search.appendGlobalRoot(rootId, std::move(files)); break;
                         }
                     });
}

void AppContext::rebuildGlobalSearch()
{
    if (!persisted().settings.enableGlobalSearch) {
        return;
    }
    globalFiles.rebuild(config.directory() / "global-search-cache.tsv", persisted().settings.globalSearchScanIntensity,
                        [this](const std::string& rootId, std::vector<GlobalFileRecord> files, GlobalFileCommitMode mode) {
                            switch (mode) {
                            case GlobalFileCommitMode::ResetAll: search.rebuildGlobalFiles({}); break;
                            case GlobalFileCommitMode::ReplaceRoot: search.replaceGlobalRoot(rootId, std::move(files)); break;
                            case GlobalFileCommitMode::AppendRoot: search.appendGlobalRoot(rootId, std::move(files)); break;
                            }
                        });
}

void AppContext::cancelGlobalSearchRebuild()
{
    // Request cancellation without blocking the UI. The worker keeps its loaded cache baseline
    // and exits after observing the request.
    globalFiles.cancel();
}

void AppContext::stopGlobalSearch()
{
    globalFiles.stop();
}

} // namespace launcher

#include "ui/MainDockDialogs.hpp"

#include "app/AppContext.hpp"
#include "ui/Localization.hpp"
#include "ui/MainDockChrome.hpp"
#include "ui/UiAnimation.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

namespace launcher {
namespace {

std::string timeText(std::int64_t value)
{
    if (value <= 0) {
        return "-";
    }
    std::time_t raw = static_cast<std::time_t>(value);
    std::tm tm{};
    localtime_s(&tm, &raw);
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm);
    return buffer;
}

std::int64_t nowUnix()
{
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::string makeTaskId()
{
    static int counter = 0;
    return "task-" + std::to_string(nowUnix()) + "-" + std::to_string(++counter);
}

const char* triggerLabel(ScheduledTriggerKind kind)
{
    switch (kind) {
    case ScheduledTriggerKind::Once: return tr("Once");
    case ScheduledTriggerKind::Weekly: return tr("Weekly");
    case ScheduledTriggerKind::Interval: return tr("Interval");
    case ScheduledTriggerKind::AppStart: return tr("App start");
    case ScheduledTriggerKind::WakeUnlock: return tr("Wake or unlock");
    case ScheduledTriggerKind::ProcessStart: return tr("Process starts");
    case ScheduledTriggerKind::Daily:
    default: return tr("Daily");
    }
}

const char* actionLabel(ScheduledActionKind kind)
{
    switch (kind) {
    case ScheduledActionKind::LaunchVirtualFolder: return tr("Run virtual folder");
    case ScheduledActionKind::LaunchItem:
    default: return tr("Run item");
    }
}

void collectTaskItems(const std::vector<LaunchItem>& items, std::vector<const LaunchItem*>& out)
{
    for (const LaunchItem& item : items) {
        if (item.type != LaunchItemType::Title && item.type != LaunchItemType::Placeholder && item.type != LaunchItemType::Note) {
            out.push_back(&item);
        }
        collectTaskItems(item.children, out);
    }
}

std::vector<const LaunchItem*> taskItems(const AppContext& context)
{
    std::vector<const LaunchItem*> items;
    for (const Category& category : context.persisted().categories) {
        collectTaskItems(category.items, items);
    }
    return items;
}

ScheduledTask makeDefaultTask(const AppContext& context)
{
    ScheduledTask task;
    task.id = makeTaskId();
    task.name = tr("New Task");
    const std::vector<const LaunchItem*> items = taskItems(context);
    if (!items.empty()) {
        task.itemId = items.front()->id;
        task.name = items.front()->name;
    }
    task.onceAt = nowUnix() + 10 * 60;
    return task;
}

void markTaskChanged(AppContext& context, ScheduledTask& task)
{
    task.nextRunAt = 0;
    task.retryAt = 0;
    task.pendingRetries = 0;
    context.save();
}

void drawTaskSectionTitle(const char* label)
{
    ImGui::Spacing();
    ImGui::TextDisabled("%s", label);
    ImGui::Separator();
    ImGui::Spacing();
}

bool drawTaskListRow(const UiPalette& theme, const ScheduledTask& task, bool selected)
{
    ImGui::PushID(task.id.c_str());
    const float rowWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    constexpr float rowHeight = 54.0f;
    const bool clicked = ImGui::InvisibleButton("task-row", ImVec2(rowWidth, rowHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImGuiID rowId = ImGui::GetItemID();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();

    const float hover = ui_anim::hoverAmount(rowId, hovered || active, 0.12f);
    ImVec4 fill = selected ? theme.header : theme.headerHovered;
    fill.w = selected ? std::max(fill.w, 0.80f) : fill.w * hover;
    if (selected || hover > 0.01f) {
        ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(fill), theme.itemRounding);
    }

    const char* title = task.name.empty() ? task.id.c_str() : task.name.c_str();
    const ImU32 titleColor = theme.text;
    const ImU32 metaColor = theme.textMuted;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ImVec2(min.x + 8.0f, min.y), ImVec2(max.x - 8.0f, max.y), true);
    dl->AddCircleFilled(ImVec2(min.x + 14.0f, min.y + 16.0f), 3.5f,
                        task.enabled ? ImGui::ColorConvertFloat4ToU32(ImVec4(0.25f, 0.72f, 0.44f, 1.0f)) : metaColor);
    dl->AddText(ImVec2(min.x + 26.0f, min.y + 8.0f), titleColor, title);
    dl->AddText(ImVec2(min.x + 26.0f, min.y + 30.0f), metaColor, triggerLabel(task.trigger));
    dl->PopClipRect();
    ImGui::PopID();
    return clicked;
}

} // namespace

void drawBuildInfoPopup(const ThemeDefinition& themeDefinition, const UiPalette& theme, bool& showBuildInfo, void (*openAppFolderFn)())
{
    if (!showBuildInfo) {
        return;
    }

    setupManagedWindow("LauncherManagedBuildInfo");
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(390.0f, 190.0f), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.popupRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, theme.windowOutlineSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.popupBg);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text));
    ImGui::PushStyleColor(ImGuiCol_Button, theme.button);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.buttonHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.buttonActive);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.windowOutline);
    bool open = true;
    if (ImGui::Begin(tr("Build Info###build-info"), &open,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings)) {
        applyManagedViewportChrome(ImGui::GetWindowViewport()->PlatformHandleRaw, themeDefinition, theme);
        drawManagedTitleBar(theme, tr("Build Info"), open);
        ImGui::SetCursorPos(ImVec2(16.0f, kUiTitleHeight + 16.0f));
        ImGui::TextUnformatted("Launcher");
        ImGui::Text(tr("Build date: %s %s"), __DATE__, __TIME__);
        ImGui::TextUnformatted(tr("Config: nlohmann/json + Dear ImGui Docking"));
        ImGui::SetCursorPos(ImVec2(168.0f, 142.0f));
        if (ImGui::Button(tr("Open Folder"), ImVec2(104.0f, 30.0f)) && openAppFolderFn) {
            openAppFolderFn();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Close"), ImVec2(86.0f, 30.0f))) {
            showBuildInfo = false;
        }
        if (!open) {
            showBuildInfo = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(3);
}

void drawDeleteConfirmPopup(const UiPalette& theme, AppContext& context, DeleteConfirmState state,
                            void (*deletePendingItemsFn)(AppContext&))
{
    if (!state.pendingDeleteIds || !state.pendingDeleteNames || !state.pendingDeleteCategory || !state.pendingDeleteCategoryName) {
        return;
    }
    if (state.openNextFrame) {
        ImGui::SetNextWindowFocus();
    }
    if (state.pendingDeleteIds->empty() && *state.pendingDeleteCategory < 0) {
        return;
    }

    setupManagedWindow("LauncherManagedDeleteConfirm");
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380.0f, 154.0f), ImGuiCond_Appearing);
    ManagedWindowStyle windowStyle(theme);

    bool open = true;
    if (ImGui::Begin(tr("Delete Confirm###delete-confirm"), &open,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings)) {
        auto cancelDelete = [&]() {
            state.pendingDeleteIds->clear();
            state.pendingDeleteNames->clear();
            *state.pendingDeleteCategory = -1;
            state.pendingDeleteCategoryName->clear();
        };
        auto confirmDelete = [&]() {
            if (deletePendingItemsFn) {
                deletePendingItemsFn(context);
            }
        };

        applyManagedViewportChrome(ImGui::GetWindowViewport()->PlatformHandleRaw, context.themes.active(), theme);
        const float width = ImGui::GetWindowWidth();
        const float height = ImGui::GetWindowHeight();
        const ImVec2 pos = ImGui::GetWindowPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (drawManagedTitleBar(theme, tr("Delete Confirm"), open) || !open) {
            cancelDelete();
        }

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
                confirmDelete();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                cancelDelete();
            }
        }

        ImGui::SetCursorPos(ImVec2(16.0f, kUiTitleHeight + 16.0f));
        if (*state.pendingDeleteCategory >= 0) {
            ImGui::Text(tr("Delete category \"%s\"?"), state.pendingDeleteCategoryName->c_str());
        } else if (state.pendingDeleteNames->size() == 1) {
            ImGui::Text(tr("Delete item \"%s\"?"), state.pendingDeleteNames->front().c_str());
        } else {
            ImGui::Text(tr("Delete %d selected items?"), static_cast<int>(state.pendingDeleteIds->size()));
        }

        dl->AddRectFilled(ImVec2(pos.x, pos.y + height - 52.0f), ImVec2(pos.x + width, pos.y + height),
                          IM_COL32(static_cast<int>(theme.childBg.x * 255.0f + 0.5f), static_cast<int>(theme.childBg.y * 255.0f + 0.5f),
                                   static_cast<int>(theme.childBg.z * 255.0f + 0.5f), static_cast<int>(theme.childBg.w * 255.0f + 0.5f)));
        ImGui::SetCursorPos(ImVec2(width - 204.0f, height - 40.0f));
        if (ImGui::Button(tr("OK"), ImVec2(90.0f, 30.0f))) {
            confirmDelete();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"), ImVec2(90.0f, 30.0f))) {
            cancelDelete();
        }
    }
    ImGui::End();
}

void drawTaskPlannerWindow(AppContext& context, const ThemeDefinition& themeDefinition, const UiPalette& theme, bool& showTaskPlanner)
{
    if (!showTaskPlanner) {
        return;
    }
    static std::string selectedTaskId;

    setupManagedWindow("LauncherManagedTaskPlanner");
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(860.0f, 560.0f), ImGuiCond_Appearing);
    ManagedWindowStyle windowStyle(theme);
    bool open = true;
    if (ImGui::Begin(tr("Task Planner###task-planner"), &open,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        applyManagedViewportChrome(ImGui::GetWindowViewport()->PlatformHandleRaw, themeDefinition, theme);
        drawManagedTitleBar(theme, tr("Task Planner"), open);
        ui_anim::pushAppearAlpha(ImGui::GetID("task-planner-appear"), 0.14f, 0.18f);

        constexpr float kOuterPadding = 18.0f;
        constexpr float kToolbarHeight = 34.0f;
        constexpr float kGap = 14.0f;
        ImGui::SetCursorPos(ImVec2(kOuterPadding, kUiTitleHeight + kOuterPadding));
        ImGui::BeginChild("task-toolbar", ImVec2(-kOuterPadding, kToolbarHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ui_anim::button(tr("Add Task"), ImVec2(112.0f, 30.0f))) {
            ScheduledTask task = makeDefaultTask(context);
            selectedTaskId = task.id;
            context.persisted().scheduledTasks.push_back(std::move(task));
            context.save();
        }
        ImGui::SameLine(0.0f, kGap);
        ImGui::TextDisabled("%s", tr("Tasks run while Launcher is running."));
        ImGui::EndChild();

        std::vector<ScheduledTask>& tasks = context.persisted().scheduledTasks;
        if (!tasks.empty() && std::none_of(tasks.begin(), tasks.end(), [](const ScheduledTask& task) {
                return task.id == selectedTaskId;
            })) {
            selectedTaskId = tasks.front().id;
        }

        const float bodyY = kUiTitleHeight + kOuterPadding + kToolbarHeight + 12.0f;
        const float bodyHeight = std::max(220.0f, ImGui::GetWindowHeight() - bodyY - kOuterPadding);
        const float bodyWidth = std::max(360.0f, ImGui::GetWindowWidth() - kOuterPadding * 2.0f);
        const float listWidth = std::clamp(bodyWidth * 0.32f, 258.0f, 318.0f);
        const float editorWidth = std::max(220.0f, bodyWidth - listWidth - kGap);

        ImGui::SetCursorPos(ImVec2(kOuterPadding, bodyY));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
        ImGui::BeginChild("task-list", ImVec2(listWidth, bodyHeight), ImGuiChildFlags_Borders);
        if (tasks.empty()) {
            ImGui::TextDisabled("%s", tr("No tasks"));
        }
        for (ScheduledTask& task : tasks) {
            const bool selected = task.id == selectedTaskId;
            if (drawTaskListRow(theme, task, selected)) {
                selectedTaskId = task.id;
            }
            ImGui::Spacing();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        ScheduledTask* selectedTask = nullptr;
        for (ScheduledTask& task : tasks) {
            if (task.id == selectedTaskId) {
                selectedTask = &task;
                break;
            }
        }
        ImGui::SameLine(0.0f, kGap);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0f, 8.0f));
        ImGui::BeginChild("task-editor", ImVec2(editorWidth, bodyHeight), ImGuiChildFlags_Borders);
        if (!selectedTask) {
            ImGui::TextDisabled("%s", tr("Select a task"));
        } else {
            ScheduledTask& task = *selectedTask;
            bool changed = false;
            changed |= ui_anim::checkbox(tr("Enabled"), &task.enabled);
            ImGui::SameLine(0.0f, kGap);
            if (ui_anim::button(tr("Run Now"), ImVec2(96.0f, 28.0f))) {
                std::string message;
                const std::int64_t startedAt = nowUnix();
                const bool ok = context.runScheduledTaskAction(task, &message);
                task.lastRunAt = startedAt;
                task.lastSuccess = ok;
                task.lastMessage = message;
                task.history.insert(task.history.begin(), ScheduledTaskHistory{startedAt, nowUnix(), ok, message});
                if (task.history.size() > 20) task.history.resize(20);
                context.save();
            }
            ImGui::SameLine(0.0f, 8.0f);
            if (ui_anim::button(tr("Delete"), ImVec2(84.0f, 28.0f))) {
                tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                                           [&](const ScheduledTask& existing) {
                                               return existing.id == task.id;
                                           }),
                            tasks.end());
                selectedTaskId = tasks.empty() ? std::string{} : tasks.front().id;
                context.save();
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ui_anim::popAppearAlpha();
                if (!open) showTaskPlanner = false;
                ImGui::End();
                return;
            }

            drawTaskSectionTitle(tr("Basic"));
            ImGui::TextUnformatted(tr("Name"));
            changed |= ImGui::InputText("##task-name", &task.name);

            int trigger = static_cast<int>(task.trigger);
            const char* triggers[] = {tr("Once"),      tr("Daily"),          tr("Weekly"),        tr("Interval"),
                                      tr("App start"), tr("Wake or unlock"), tr("Process starts")};
            if (ImGui::Combo(tr("Trigger"), &trigger, triggers, IM_ARRAYSIZE(triggers))) {
                task.trigger = static_cast<ScheduledTriggerKind>(trigger);
                changed = true;
            }

            int action = static_cast<int>(task.action);
            const char* actions[] = {tr("Run item"), tr("Run virtual folder")};
            if (ImGui::Combo(tr("Action"), &action, actions, IM_ARRAYSIZE(actions))) {
                task.action = static_cast<ScheduledActionKind>(action);
                changed = true;
            }

            const std::vector<const LaunchItem*> items = taskItems(context);
            int itemIndex = 0;
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (items[static_cast<size_t>(i)]->id == task.itemId) {
                    itemIndex = i;
                    break;
                }
            }
            if (ImGui::BeginCombo(tr("Item"), items.empty() ? tr("Not set") : items[static_cast<size_t>(itemIndex)]->name.c_str())) {
                for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                    const LaunchItem* item = items[static_cast<size_t>(i)];
                    const bool selected = i == itemIndex;
                    std::string label = item->name + "###" + item->id;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        task.itemId = item->id;
                        if (task.name.empty() || task.name == tr("New Task")) {
                            task.name = item->name;
                        }
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }

            drawTaskSectionTitle(tr("Schedule"));
            if (task.trigger == ScheduledTriggerKind::Once) {
                int delayMinutes = task.onceAt > nowUnix() ? static_cast<int>((task.onceAt - nowUnix()) / 60) : 10;
                if (ImGui::InputInt(tr("Run after minutes"), &delayMinutes)) {
                    task.onceAt = nowUnix() + static_cast<std::int64_t>(std::max(1, delayMinutes)) * 60;
                    changed = true;
                }
            }
            if (task.trigger == ScheduledTriggerKind::Daily || task.trigger == ScheduledTriggerKind::Weekly) {
                changed |= ui_anim::sliderInt(tr("Hour"), &task.hour, 0, 23);
                changed |= ui_anim::sliderInt(tr("Minute"), &task.minute, 0, 59);
            }
            if (task.trigger == ScheduledTriggerKind::Weekly) {
                const char* days[] = {tr("Sun"), tr("Mon"), tr("Tue"), tr("Wed"), tr("Thu"), tr("Fri"), tr("Sat")};
                for (int i = 0; i < 7; ++i) {
                    bool on = (task.weekdayMask & (1 << i)) != 0;
                    if (ui_anim::checkbox(days[i], &on)) {
                        if (on)
                            task.weekdayMask |= (1 << i);
                        else
                            task.weekdayMask &= ~(1 << i);
                        changed = true;
                    }
                    if (i < 6) ImGui::SameLine();
                }
            }
            if (task.trigger == ScheduledTriggerKind::Interval) {
                changed |= ImGui::InputInt(tr("Interval minutes"), &task.intervalMinutes);
                task.intervalMinutes = std::max(1, task.intervalMinutes);
            }
            if (task.trigger == ScheduledTriggerKind::ProcessStart) {
                ImGui::TextUnformatted(tr("Process name"));
                changed |= ImGui::InputText("##process-name", &task.processName);
            }
            drawTaskSectionTitle(tr("Options"));
            changed |= ui_anim::checkbox(tr("Run missed tasks"), &task.runMissed);
            changed |= ui_anim::checkbox(tr("Run minimized"), &task.runMinimized);
            changed |= ImGui::InputInt(tr("Retry count"), &task.retryCount);
            task.retryCount = std::clamp(task.retryCount, 0, 10);
            changed |= ImGui::InputInt(tr("Retry delay seconds"), &task.retryDelaySeconds);
            task.retryDelaySeconds = std::clamp(task.retryDelaySeconds, 1, 3600);
            if (changed) {
                markTaskChanged(context, task);
            }

            drawTaskSectionTitle(tr("Status"));
            ImGui::Text("%s: %s", tr("Next run"), timeText(task.nextRunAt).c_str());
            ImGui::Text("%s: %s", tr("Last run at"), timeText(task.lastRunAt).c_str());
            ImGui::Text("%s: %s", tr("Last result"), task.lastMessage.empty() ? "-" : task.lastMessage.c_str());
            drawTaskSectionTitle(tr("History"));
            if (task.history.empty()) {
                ImGui::TextDisabled("%s", tr("No history"));
            } else if (ImGui::BeginTable("task-history", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn(tr("Time"));
                ImGui::TableSetupColumn(tr("Result"));
                ImGui::TableSetupColumn(tr("Message"));
                ImGui::TableHeadersRow();
                for (const ScheduledTaskHistory& history : task.history) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(timeText(history.startedAt).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(history.success ? tr("Success") : tr("Failed"));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(history.message.c_str());
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ui_anim::popAppearAlpha();
        if (!open) {
            showTaskPlanner = false;
        }
    }
    ImGui::End();
}

} // namespace launcher

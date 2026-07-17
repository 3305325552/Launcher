#include "ui/MainDockState.hpp"

#include "app/AppContext.hpp"
#include "ui/MainDockDragPayload.hpp"
#include <windows.h>
#include <imgui.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <optional>
#include <unordered_set>

namespace launcher {
namespace {

struct MainDockRuntimeState {
    MainDockStateApi api;

    std::optional<LaunchItem> clipboardItem;
    bool clipboardCut = false;
    std::string clipboardSourceId;

    std::string dragHoverTarget;
    double dragHoverStartedAt = 0.0;

    std::unordered_set<std::string> selectedItemIds;
    std::string lastSelectedItemId;
    std::string dragPrimaryItemId;
    std::vector<std::string> dragItemIdsSnapshot;
    std::string pressItemId;
    bool pressWasMulti = false;
    bool pressHandledAsDrag = false;

    std::vector<std::string> pendingDeleteIds;
    std::vector<std::string> pendingDeleteNames;
    int pendingDeleteCategory = -1;
    std::string pendingDeleteCategoryName;
    bool openDeleteConfirm = false;
};

MainDockRuntimeState gState;

constexpr double kDragHoverOpenDelay = 0.90;

const Category* selectedCategory(const AppContext& context)
{
    const PersistedState& persisted = context.persisted();
    const RuntimeState& runtime = context.runtime();
    if (runtime.selectedCategory < 0 || runtime.selectedCategory >= static_cast<int>(persisted.categories.size())) {
        return nullptr;
    }
    return &persisted.categories[runtime.selectedCategory];
}

Category* selectedCategory(AppContext& context)
{
    PersistedState& persisted = context.persisted();
    const RuntimeState& runtime = context.runtime();
    if (runtime.selectedCategory < 0 || runtime.selectedCategory >= static_cast<int>(persisted.categories.size())) {
        return nullptr;
    }
    return &persisted.categories[runtime.selectedCategory];
}

const LaunchItem* findFolderForStack(const Category& category, const std::vector<std::string>& stack)
{
    const std::vector<LaunchItem>* items = &category.items;
    const LaunchItem* folder = nullptr;
    for (const std::string& id : stack) {
        folder = nullptr;
        for (const LaunchItem& item : *items) {
            if (item.id == id && item.type == LaunchItemType::VirtualFolder) {
                folder = &item;
                break;
            }
        }
        if (!folder) {
            return nullptr;
        }
        items = &folder->children;
    }
    return folder;
}

LaunchItem* findFolderForStack(Category& category, const std::vector<std::string>& stack)
{
    std::vector<LaunchItem>* items = &category.items;
    LaunchItem* folder = nullptr;
    for (const std::string& id : stack) {
        folder = nullptr;
        for (LaunchItem& item : *items) {
            if (item.id == id && item.type == LaunchItemType::VirtualFolder) {
                folder = &item;
                break;
            }
        }
        if (!folder) {
            return nullptr;
        }
        items = &folder->children;
    }
    return folder;
}

} // namespace

void configureMainDockState(const MainDockStateApi& api)
{
    gState.api = api;
}

bool listLayoutLockedForStack(const AppContext& context, const std::vector<std::string>& stack)
{
    const Category* category = selectedCategory(context);
    if (!category) {
        return false;
    }
    if (stack.empty()) {
        return category->lockLayout;
    }
    if (const LaunchItem* folder = findFolderForStack(*category, stack)) {
        return folder->lockLayout;
    }
    return false;
}

bool currentListLayoutLocked(const AppContext& context)
{
    return listLayoutLockedForStack(context, context.runtime().currentFolderStack);
}

void setCurrentListLayoutLocked(AppContext& context, bool locked)
{
    Category* category = selectedCategory(context);
    if (!category) {
        return;
    }
    RuntimeState& runtime = context.runtime();
    if (runtime.currentFolderStack.empty()) {
        category->lockLayout = locked;
        return;
    }
    if (LaunchItem* folder = findFolderForStack(*category, runtime.currentFolderStack)) {
        folder->lockLayout = locked;
    }
}

void triggerAfterDragHover(const std::string& key, const std::function<void()>& callback)
{
    const double now = ImGui::GetTime();
    if (gState.dragHoverTarget != key) {
        gState.dragHoverTarget = key;
        gState.dragHoverStartedAt = now;
        return;
    }
    if (now - gState.dragHoverStartedAt >= kDragHoverOpenDelay) {
        callback();
        gState.dragHoverStartedAt = now + 3600.0;
    }
}

bool dragHoverPending(const std::string& key)
{
    return !key.empty() && gState.dragHoverTarget == key;
}

void clearDragHoverState()
{
    gState.dragHoverTarget.clear();
    gState.dragHoverStartedAt = 0.0;
}

bool isItemSelected(const LaunchItem& item)
{
    return gState.selectedItemIds.contains(item.id);
}

int selectedItemCount()
{
    return static_cast<int>(gState.selectedItemIds.size());
}

void clearSelection(AppContext& context)
{
    gState.selectedItemIds.clear();
    gState.lastSelectedItemId.clear();
    context.runtime().selectedItemId.clear();
}

void selectIds(AppContext& context, const std::vector<std::string>& ids, const std::string& activeId)
{
    gState.selectedItemIds.clear();
    for (const std::string& id : ids) {
        if (!id.empty()) {
            gState.selectedItemIds.insert(id);
        }
    }
    gState.lastSelectedItemId = activeId;
    context.runtime().selectedItemId = activeId;
}

void selectSingle(AppContext& context, const LaunchItem& item)
{
    gState.selectedItemIds.clear();
    gState.selectedItemIds.insert(item.id);
    gState.lastSelectedItemId = item.id;
    context.runtime().selectedItemId = item.id;
}

void toggleSelection(AppContext& context, const LaunchItem& item)
{
    if (gState.selectedItemIds.contains(item.id)) {
        gState.selectedItemIds.erase(item.id);
        RuntimeState& runtime = context.runtime();
        if (runtime.selectedItemId == item.id) {
            runtime.selectedItemId = gState.selectedItemIds.empty() ? "" : *gState.selectedItemIds.begin();
        }
    } else {
        gState.selectedItemIds.insert(item.id);
        context.runtime().selectedItemId = item.id;
        gState.lastSelectedItemId = item.id;
    }
    if (gState.selectedItemIds.empty()) {
        gState.lastSelectedItemId.clear();
    }
}

void selectRange(AppContext& context, const std::vector<LaunchItem>& items, int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= static_cast<int>(items.size())) {
        return;
    }

    int anchor = -1;
    if (!gState.lastSelectedItemId.empty()) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[i].id == gState.lastSelectedItemId) {
                anchor = i;
                break;
            }
        }
    }
    if (anchor < 0) {
        selectSingle(context, items[itemIndex]);
        return;
    }

    const int begin = std::min(anchor, itemIndex);
    const int end = std::max(anchor, itemIndex);
    for (int i = begin; i <= end; ++i) {
        gState.selectedItemIds.insert(items[i].id);
    }
    context.runtime().selectedItemId = items[itemIndex].id;
}

void handleItemSelectionClick(AppContext& context, const std::vector<LaunchItem>& items, int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= static_cast<int>(items.size())) {
        return;
    }
    const LaunchItem& item = items[itemIndex];
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift) {
        selectRange(context, items, itemIndex);
        gState.pressItemId.clear();
        gState.pressWasMulti = false;
        gState.pressHandledAsDrag = false;
        return;
    }
    if (io.KeyCtrl) {
        toggleSelection(context, item);
        gState.pressItemId.clear();
        gState.pressWasMulti = false;
        gState.pressHandledAsDrag = false;
        return;
    }

    // Plain press on a multi-selected item: keep multi until release/drag decision.
    // This allows dragging the whole multi-selection while still collapsing on a real click.
    if (gState.selectedItemIds.contains(item.id) && selectedItemCount() > 1) {
        gState.pressItemId = item.id;
        gState.pressWasMulti = true;
        gState.pressHandledAsDrag = false;
        context.runtime().selectedItemId = item.id;
        gState.lastSelectedItemId = item.id;
        return;
    }

    selectSingle(context, item);
    gState.pressItemId.clear();
    gState.pressWasMulti = false;
    gState.pressHandledAsDrag = false;
}

void resolvePendingItemSelectionClick(AppContext& context)
{
    if (gState.pressItemId.empty() || !gState.pressWasMulti || gState.pressHandledAsDrag) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !(ImGui::GetDragDropPayload() && ImGui::GetDragDropPayload()->IsDataType(drag_payload::kItemId))) {
            gState.pressItemId.clear();
            gState.pressWasMulti = false;
            gState.pressHandledAsDrag = false;
        }
        return;
    }

    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    const bool dragActive = payload && payload->IsDataType(drag_payload::kItemId);
    if (dragActive) {
        gState.pressHandledAsDrag = true;
        gState.pressItemId.clear();
        gState.pressWasMulti = false;
        return;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (gState.api.findItemById) {
            if (LaunchItem* item = gState.api.findItemById(context, gState.pressItemId)) {
                selectSingle(context, *item);
            }
        }
        gState.pressItemId.clear();
        gState.pressWasMulti = false;
        gState.pressHandledAsDrag = false;
    }
}

std::vector<std::string> selectedItemIds(const AppContext& context)
{
    std::vector<std::string> ids;
    for (const std::string& id : gState.selectedItemIds) {
        if (gState.api.findItemByIdConst && gState.api.findItemByIdConst(context, id)) {
            ids.push_back(id);
        }
    }
    const RuntimeState& runtime = context.runtime();
    if (ids.empty() && !runtime.selectedItemId.empty() && gState.api.findItemByIdConst &&
        gState.api.findItemByIdConst(context, runtime.selectedItemId)) {
        ids.push_back(runtime.selectedItemId);
    }
    return ids;
}

void captureDragItemIds(const AppContext& context, const std::string& primaryId)
{
    gState.dragPrimaryItemId.clear();
    gState.dragItemIdsSnapshot.clear();
    gState.pressHandledAsDrag = true;
    gState.pressItemId.clear();
    gState.pressWasMulti = false;
    if (primaryId.empty()) {
        return;
    }
    if (!gState.selectedItemIds.contains(primaryId) || gState.selectedItemIds.size() <= 1) {
        gState.dragPrimaryItemId = primaryId;
        gState.dragItemIdsSnapshot = {primaryId};
        return;
    }
    if (gState.api.currentItems) {
        if (const std::vector<LaunchItem>* items = gState.api.currentItems(const_cast<AppContext&>(context))) {
            gState.dragItemIdsSnapshot.reserve(gState.selectedItemIds.size());
            for (const LaunchItem& item : *items) {
                if (gState.selectedItemIds.contains(item.id)) {
                    gState.dragItemIdsSnapshot.push_back(item.id);
                }
            }
        }
    }
    if (gState.dragItemIdsSnapshot.empty()) {
        gState.dragItemIdsSnapshot = {primaryId};
    }
    gState.dragPrimaryItemId = primaryId;
}

std::vector<std::string> dragItemIds(const AppContext& context, const std::string& primaryId)
{
    if (primaryId.empty()) {
        return {};
    }
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    const bool dragActive = payload && payload->IsDataType(drag_payload::kItemId);
    if (!dragActive) {
        gState.dragPrimaryItemId.clear();
        gState.dragItemIdsSnapshot.clear();
    }
    if (dragActive && gState.dragPrimaryItemId == primaryId && !gState.dragItemIdsSnapshot.empty()) {
        return gState.dragItemIdsSnapshot;
    }
    captureDragItemIds(context, primaryId);
    return gState.dragItemIdsSnapshot.empty() ? std::vector<std::string>{primaryId} : gState.dragItemIdsSnapshot;
}

void clearDragItemIdsSnapshot()
{
    gState.dragPrimaryItemId.clear();
    gState.dragItemIdsSnapshot.clear();
}

bool clipboardAvailable()
{
    return gState.clipboardItem.has_value();
}

void clearClipboardState()
{
    gState.clipboardItem.reset();
    gState.clipboardCut = false;
    gState.clipboardSourceId.clear();
}

void pasteClipboardItem(AppContext& context)
{
    std::vector<LaunchItem>* items = gState.api.currentItems ? gState.api.currentItems(context) : nullptr;
    if (!items || !gState.clipboardItem) {
        return;
    }

    LaunchItem item = *gState.clipboardItem;
    if (gState.clipboardCut) {
        if (gState.api.removeItemById) {
            gState.api.removeItemById(context, gState.clipboardSourceId);
        }
        gState.clipboardCut = false;
        gState.clipboardSourceId.clear();
    } else {
        item.id = "item-" + std::to_string(static_cast<int>(items->size()) + 1) + "-" +
                  std::to_string(gState.api.nowUnix ? gState.api.nowUnix() : 0);
        item.createdAt = gState.api.nowUnix ? gState.api.nowUnix() : item.createdAt;
    }
    item.lastEditedAt = gState.api.nowUnix ? gState.api.nowUnix() : item.lastEditedAt;
    items->push_back(item);
    selectSingle(context, items->back());
    context.commitContentChange();
}

void copyItemToClipboard(const LaunchItem& item, bool cut)
{
    gState.clipboardItem = item;
    gState.clipboardCut = cut;
    gState.clipboardSourceId = item.id;
    if (!item.target.empty()) {
        ImGui::SetClipboardText(item.target.string().c_str());
    }
}

void copyTextToClipboard(const std::string& text)
{
    ImGui::SetClipboardText(text.c_str());
}

void clearDeleteState()
{
    gState.pendingDeleteIds.clear();
    gState.pendingDeleteNames.clear();
    gState.pendingDeleteCategory = -1;
    gState.pendingDeleteCategoryName.clear();
    gState.openDeleteConfirm = false;
}

bool hasPendingDeleteState()
{
    return gState.openDeleteConfirm || !gState.pendingDeleteIds.empty() || gState.pendingDeleteCategory >= 0;
}

void requestDeleteIds(const AppContext& context, std::vector<std::string> ids)
{
    if (ids.empty()) {
        return;
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    gState.pendingDeleteIds = std::move(ids);
    gState.pendingDeleteNames.clear();
    for (const std::string& id : gState.pendingDeleteIds) {
        if (gState.api.findItemByIdConst) {
            if (const LaunchItem* item = gState.api.findItemByIdConst(context, id)) {
                gState.pendingDeleteNames.push_back(item->name.empty() ? "New Item" : item->name);
            }
        }
    }
    gState.openDeleteConfirm = true;
}

void requestDeleteSelection(const AppContext& context)
{
    requestDeleteIds(context, selectedItemIds(context));
}

void requestDeleteCategory(const AppContext& context, int index)
{
    const PersistedState& persisted = context.persisted();
    if (index < 0 || index >= static_cast<int>(persisted.categories.size()) || persisted.categories.size() <= 1) {
        return;
    }
    gState.pendingDeleteIds.clear();
    gState.pendingDeleteNames.clear();
    gState.pendingDeleteCategory = index;
    gState.pendingDeleteCategoryName = persisted.categories[index].name;
    gState.openDeleteConfirm = true;
}

DeleteConfirmState takeDeleteConfirmState()
{
    DeleteConfirmState state{gState.openDeleteConfirm, &gState.pendingDeleteIds, &gState.pendingDeleteNames, &gState.pendingDeleteCategory,
                             &gState.pendingDeleteCategoryName};
    gState.openDeleteConfirm = false;
    return state;
}

void deletePendingItems(AppContext& context)
{
    if (gState.pendingDeleteCategory >= 0) {
        PersistedState& persisted = context.persisted();
        RuntimeState& runtime = context.runtime();
        if (gState.pendingDeleteCategory < static_cast<int>(persisted.categories.size()) && persisted.categories.size() > 1) {
            persisted.categories.erase(persisted.categories.begin() + gState.pendingDeleteCategory);
            runtime.selectedCategory = std::clamp(runtime.selectedCategory, 0, static_cast<int>(persisted.categories.size()) - 1);
            clearSelection(context);
            context.commitContentChange();
        }
        gState.pendingDeleteCategory = -1;
        gState.pendingDeleteCategoryName.clear();
        return;
    }
    if (gState.pendingDeleteIds.empty()) {
        return;
    }
    const std::unordered_set<std::string> pending(gState.pendingDeleteIds.begin(), gState.pendingDeleteIds.end());
    for (Category& category : context.persisted().categories) {
        if (gState.api.removeItemById) {
            for (const std::string& id : pending) {
                gState.api.removeItemById(context, id);
            }
        }
    }
    gState.selectedItemIds.clear();
    gState.lastSelectedItemId.clear();
    if (pending.contains(context.runtime().selectedItemId)) {
        context.runtime().selectedItemId.clear();
    }
    gState.pendingDeleteIds.clear();
    gState.pendingDeleteNames.clear();
    context.commitContentChange();
}

void rebuildIconCacheForSelection(AppContext& context, const LaunchItem& fallback)
{
    std::vector<std::string> ids = selectedItemIds(context);
    if (ids.empty()) {
        if (gState.api.clearIconCacheForItem) {
            gState.api.clearIconCacheForItem(fallback);
        }
    } else {
        for (const std::string& id : ids) {
            if (gState.api.findItemByIdConst) {
                if (const LaunchItem* item = gState.api.findItemByIdConst(context, id)) {
                    if (gState.api.clearIconCacheForItem) {
                        gState.api.clearIconCacheForItem(*item);
                    }
                }
            }
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

} // namespace launcher

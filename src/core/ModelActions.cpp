#include "core/ModelActions.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

namespace launcher::model_actions {
namespace {

bool ensureUniqueItemIds(std::vector<LaunchItem>& items, std::unordered_set<std::string>& usedIds, int& nextId)
{
    bool changed = false;
    for (LaunchItem& item : items) {
        if (item.id.empty() || usedIds.contains(item.id)) {
            do {
                item.id = "item-migrated-" + std::to_string(++nextId);
            } while (usedIds.contains(item.id));
            changed = true;
        }
        usedIds.insert(item.id);
        if (ensureUniqueItemIds(item.children, usedIds, nextId)) {
            changed = true;
        }
    }
    return changed;
}

bool moveVectorItemToVisibleInsert(std::vector<LaunchItem>& items, int from, int insertAt)
{
    if (from < 0 || from >= static_cast<int>(items.size())) {
        return false;
    }
    LaunchItem moved = std::move(items[from]);
    items.erase(items.begin() + from);
    insertAt = std::clamp(insertAt, 0, static_cast<int>(items.size()));
    items.insert(items.begin() + insertAt, std::move(moved));
    return true;
}

} // namespace

bool ensureUniqueIds(PersistedState& state)
{
    std::unordered_set<std::string> usedIds;
    int nextId = 0;
    bool changed = false;
    for (Category& category : state.categories) {
        if (ensureUniqueItemIds(category.items, usedIds, nextId)) {
            changed = true;
        }
    }
    return changed;
}

LaunchItem* findItemInList(std::vector<LaunchItem>& items, const std::string& id)
{
    for (LaunchItem& item : items) {
        if (item.id == id) {
            return &item;
        }
        if (LaunchItem* child = findItemInList(item.children, id)) {
            return child;
        }
    }
    return nullptr;
}

const LaunchItem* findItemInList(const std::vector<LaunchItem>& items, const std::string& id)
{
    for (const LaunchItem& item : items) {
        if (item.id == id) {
            return &item;
        }
        if (const LaunchItem* child = findItemInList(item.children, id)) {
            return child;
        }
    }
    return nullptr;
}

namespace {

std::vector<LaunchItem>* findOwnerList(std::vector<LaunchItem>& items, const std::string& id)
{
    for (LaunchItem& item : items) {
        if (item.id == id) {
            return &items;
        }
        if (std::vector<LaunchItem>* owner = findOwnerList(item.children, id)) {
            return owner;
        }
    }
    return nullptr;
}

} // namespace

int itemIndexById(const std::vector<LaunchItem>& items, const std::string& id)
{
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (items[i].id == id) {
            return i;
        }
    }
    return -1;
}

namespace {

LaunchItem* findItem(PersistedState& persisted, const std::string& id)
{
    for (Category& category : persisted.categories) {
        if (LaunchItem* item = findItemInList(category.items, id)) {
            return item;
        }
    }
    return nullptr;
}

std::string folderIdForChildrenList(std::vector<LaunchItem>& items, std::vector<LaunchItem>& list)
{
    for (LaunchItem& item : items) {
        if (&item.children == &list) {
            return item.id;
        }
        std::string nested = folderIdForChildrenList(item.children, list);
        if (!nested.empty()) {
            return nested;
        }
    }
    return {};
}

std::string folderIdForChildrenList(PersistedState& persisted, std::vector<LaunchItem>& list)
{
    for (Category& category : persisted.categories) {
        std::string id = folderIdForChildrenList(category.items, list);
        if (!id.empty()) {
            return id;
        }
    }
    return {};
}

} // namespace

bool reorderCategory(PersistedState& persisted, RuntimeState& runtime, int from, int insertAt)
{
    std::vector<Category>& categories = persisted.categories;
    if (from < 0 || from >= static_cast<int>(categories.size())) {
        return false;
    }

    const std::string selectedId = runtime.selectedCategory >= 0 && runtime.selectedCategory < static_cast<int>(categories.size())
                                       ? categories[runtime.selectedCategory].id
                                       : std::string{};
    insertAt = std::clamp(insertAt, 0, static_cast<int>(categories.size()));
    if (from < insertAt) {
        --insertAt;
    }
    if (from == insertAt) {
        return false;
    }

    Category moved = std::move(categories[from]);
    categories.erase(categories.begin() + from);
    categories.insert(categories.begin() + insertAt, std::move(moved));

    if (!selectedId.empty()) {
        for (int i = 0; i < static_cast<int>(categories.size()); ++i) {
            if (categories[i].id == selectedId) {
                runtime.selectedCategory = i;
                break;
            }
        }
    }
    return true;
}

bool reorderItem(std::vector<LaunchItem>& items, const AppSettings& settings, bool listLocked, int from, int insertAt)
{
    if (settings.sortMode != SortMode::Free || settings.lockItemLayout || listLocked || from < 0 ||
        from >= static_cast<int>(items.size())) {
        return false;
    }

    insertAt = std::clamp(insertAt, 0, static_cast<int>(items.size()));
    if (from < insertAt) {
        --insertAt;
    }
    if (from == insertAt) {
        return false;
    }

    LaunchItem moved = std::move(items[from]);
    items.erase(items.begin() + from);
    items.insert(items.begin() + insertAt, std::move(moved));
    return true;
}

MoveItemsResult moveItemByIdToList(PersistedState& persisted, const std::string& id, std::vector<LaunchItem>& destination, int insertAt)
{
    MoveItemsResult result;
    if (id.empty() || persisted.settings.lockItemLayout) {
        return result;
    }

    std::vector<LaunchItem>* source = nullptr;
    int sourceIndex = -1;
    for (Category& category : persisted.categories) {
        if (std::vector<LaunchItem>* owner = findOwnerList(category.items, id)) {
            source = owner;
            sourceIndex = itemIndexById(*owner, id);
            break;
        }
    }
    if (!source || sourceIndex < 0) {
        return result;
    }

    if (source == &destination) {
        if (insertAt < 0) {
            insertAt = static_cast<int>(destination.size());
        }
        if (moveVectorItemToVisibleInsert(destination, sourceIndex, insertAt)) {
            result.changed = true;
            result.movedIds = {id};
        }
        return result;
    }

    const std::string destinationFolderId = folderIdForChildrenList(persisted, destination);
    if (!destinationFolderId.empty() &&
        (id == destinationFolderId || findItemInList((*source)[sourceIndex].children, destinationFolderId))) {
        return result;
    }

    LaunchItem moved = std::move((*source)[sourceIndex]);
    source->erase(source->begin() + sourceIndex);
    std::vector<LaunchItem>* target = nullptr;
    if (!destinationFolderId.empty()) {
        LaunchItem* folder = findItem(persisted, destinationFolderId);
        if (!folder || folder->type != LaunchItemType::VirtualFolder) {
            source->insert(source->begin() + std::min(sourceIndex, static_cast<int>(source->size())), std::move(moved));
            return result;
        }
        target = &folder->children;
    } else {
        target = &destination;
    }
    if (insertAt < 0 || insertAt > static_cast<int>(target->size())) {
        insertAt = static_cast<int>(target->size());
    }
    insertAt = std::clamp(insertAt, 0, static_cast<int>(target->size()));
    target->insert(target->begin() + insertAt, std::move(moved));
    result.changed = true;
    result.movedAcrossLists = true;
    result.movedIds = {id};
    return result;
}

MoveItemsResult moveItemIdsToList(PersistedState& persisted, const std::vector<std::string>& ids, std::vector<LaunchItem>& destination,
                                  int insertAt)
{
    MoveItemsResult result;
    if (ids.empty() || persisted.settings.lockItemLayout) {
        return result;
    }

    std::unordered_set<std::string> uniqueIds;
    uniqueIds.reserve(ids.size());
    for (const std::string& id : ids) {
        if (!id.empty()) {
            uniqueIds.insert(id);
        }
    }
    if (uniqueIds.empty()) {
        return result;
    }
    if (uniqueIds.size() == 1) {
        return moveItemByIdToList(persisted, *uniqueIds.begin(), destination, insertAt);
    }

    std::vector<LaunchItem>* source = nullptr;
    for (Category& category : persisted.categories) {
        for (const std::string& id : uniqueIds) {
            if (std::vector<LaunchItem>* owner = findOwnerList(category.items, id)) {
                if (!source) {
                    source = owner;
                } else if (source != owner) {
                    return result;
                }
            }
        }
    }
    if (!source) {
        return result;
    }

    std::vector<LaunchItem> movedItems;
    std::vector<LaunchItem> remainingItems;
    movedItems.reserve(uniqueIds.size());
    remainingItems.reserve(source->size());
    for (LaunchItem& item : *source) {
        if (uniqueIds.contains(item.id)) {
            movedItems.push_back(std::move(item));
        } else {
            remainingItems.push_back(std::move(item));
        }
    }
    if (movedItems.empty()) {
        return result;
    }

    const std::string destinationFolderId = folderIdForChildrenList(persisted, destination);
    if (!destinationFolderId.empty()) {
        for (const LaunchItem& moved : movedItems) {
            if (moved.id == destinationFolderId || findItemInList(moved.children, destinationFolderId)) {
                return result;
            }
        }
    }

    if (source == &destination) {
        if (insertAt < 0 || insertAt > static_cast<int>(remainingItems.size())) {
            insertAt = static_cast<int>(remainingItems.size());
        }
        insertAt = std::clamp(insertAt, 0, static_cast<int>(remainingItems.size()));
        remainingItems.insert(remainingItems.begin() + insertAt, std::make_move_iterator(movedItems.begin()),
                              std::make_move_iterator(movedItems.end()));
        destination = std::move(remainingItems);
    } else {
        *source = std::move(remainingItems);
        std::vector<LaunchItem>* target = nullptr;
        if (!destinationFolderId.empty()) {
            LaunchItem* folder = findItem(persisted, destinationFolderId);
            if (!folder || folder->type != LaunchItemType::VirtualFolder) {
                return result;
            }
            target = &folder->children;
        } else {
            target = &destination;
        }
        if (insertAt < 0 || insertAt > static_cast<int>(target->size())) {
            insertAt = static_cast<int>(target->size());
        }
        insertAt = std::clamp(insertAt, 0, static_cast<int>(target->size()));
        target->insert(target->begin() + insertAt, std::make_move_iterator(movedItems.begin()), std::make_move_iterator(movedItems.end()));
        result.movedAcrossLists = true;
    }

    result.changed = true;
    result.movedIds.reserve(uniqueIds.size());
    for (const std::string& id : ids) {
        if (uniqueIds.contains(id)) {
            result.movedIds.push_back(id);
            uniqueIds.erase(id);
        }
    }
    if (result.movedIds.empty()) {
        for (const LaunchItem& item : movedItems) {
            result.movedIds.push_back(item.id);
        }
    }
    return result;
}

} // namespace launcher::model_actions

#pragma once

#include "launcher/Models.hpp"

namespace launcher::model_actions {

struct MoveItemsResult {
    bool changed = false;
    bool movedAcrossLists = false;
    std::vector<std::string> movedIds;
};

bool ensureUniqueIds(PersistedState& state);
LaunchItem* findItemInList(std::vector<LaunchItem>& items, const std::string& id);
const LaunchItem* findItemInList(const std::vector<LaunchItem>& items, const std::string& id);
int itemIndexById(const std::vector<LaunchItem>& items, const std::string& id);
bool reorderCategory(PersistedState& persisted, RuntimeState& runtime, int from, int insertAt);
bool reorderItem(std::vector<LaunchItem>& items, const AppSettings& settings, bool listLocked, int from, int insertAt);
MoveItemsResult moveItemByIdToList(PersistedState& persisted, const std::string& id, std::vector<LaunchItem>& destination,
                                   int insertAt = -1);
MoveItemsResult moveItemIdsToList(PersistedState& persisted, const std::vector<std::string>& ids, std::vector<LaunchItem>& destination,
                                  int insertAt = -1);

} // namespace launcher::model_actions

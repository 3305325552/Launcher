#pragma once

#include "launcher/Models.hpp"

#include <vector>

namespace launcher::main_dock {

enum class NavigationDirection {
    Left,
    Right,
    Up,
    Down
};

struct NavigationEntry {
    int itemIndex = -1;
    int row = 0;
    int column = 0;
};

std::vector<NavigationEntry> buildNavigationEntries(const std::vector<LaunchItem>& items, ItemViewMode viewMode, SortMode sortMode,
                                                    int iconSize, float availableWidth);
int findNavigationTarget(const std::vector<NavigationEntry>& entries, int currentEntry, ItemViewMode viewMode,
                         NavigationDirection direction);

} // namespace launcher::main_dock

#include "ui/dock/MainDockNavigation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace launcher::main_dock {
namespace {

std::vector<int> orderedItemIndices(const std::vector<LaunchItem>& items, SortMode mode)
{
    std::vector<int> indices(items.size());
    for (int i = 0; i < static_cast<int>(indices.size()); ++i) {
        indices[i] = i;
    }
    if (mode == SortMode::Free) {
        return indices;
    }

    std::stable_sort(indices.begin(), indices.end(), [&](int lhs, int rhs) {
        const LaunchItem& a = items[static_cast<size_t>(lhs)];
        const LaunchItem& b = items[static_cast<size_t>(rhs)];
        switch (mode) {
        case SortMode::Name: return a.name < b.name;
        case SortMode::Type: return static_cast<int>(a.type) < static_cast<int>(b.type);
        case SortMode::RunCount: return a.runCount > b.runCount;
        case SortMode::CreatedAt: return a.createdAt > b.createdAt;
        case SortMode::LastRunAt: return a.lastRunAt > b.lastRunAt;
        case SortMode::LastEditedAt: return a.lastEditedAt > b.lastEditedAt;
        case SortMode::Free:
        default: return lhs < rhs;
        }
    });
    return indices;
}

int closestEntryInRow(const std::vector<NavigationEntry>& entries, int row, int preferredColumn)
{
    int best = -1;
    int bestDistance = std::numeric_limits<int>::max();
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (entries[static_cast<size_t>(i)].row != row) {
            continue;
        }
        const int distance = std::abs(entries[static_cast<size_t>(i)].column - preferredColumn);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

} // namespace

std::vector<NavigationEntry> buildNavigationEntries(const std::vector<LaunchItem>& items, ItemViewMode viewMode, SortMode sortMode,
                                                    int iconSize, float availableWidth)
{
    const std::vector<int> order = orderedItemIndices(items, sortMode);
    std::vector<NavigationEntry> entries;
    entries.reserve(order.size());
    if (viewMode == ItemViewMode::List) {
        for (int row = 0; row < static_cast<int>(order.size()); ++row) {
            entries.push_back({order[static_cast<size_t>(row)], row, 0});
        }
        return entries;
    }

    const float clampedIconSize = static_cast<float>(std::clamp(iconSize, 24, 96));
    const bool tileMode = viewMode == ItemViewMode::Tile;
    const float tileWidth = tileMode ? std::max(120.0f, clampedIconSize + 92.0f) : std::clamp(clampedIconSize + 40.0f, 72.0f, 128.0f);
    const float gap = clampedIconSize <= 40.0f ? 6.0f : clampedIconSize >= 56.0f ? 18.0f : 12.0f;
    const int columns = std::max(1, static_cast<int>(std::max(0.0f, availableWidth) / (tileWidth + gap)));

    int row = 0;
    int column = 0;
    for (int itemIndex : order) {
        const LaunchItem& item = items[static_cast<size_t>(itemIndex)];
        if (item.type == LaunchItemType::Title) {
            if (column != 0) {
                ++row;
                column = 0;
            }
            entries.push_back({itemIndex, row, 0});
            ++row;
            continue;
        }
        entries.push_back({itemIndex, row, column});
        if (++column >= columns) {
            column = 0;
            ++row;
        }
    }
    return entries;
}

int findNavigationTarget(const std::vector<NavigationEntry>& entries, int currentEntry, ItemViewMode viewMode,
                         NavigationDirection direction)
{
    if (currentEntry < 0 || currentEntry >= static_cast<int>(entries.size())) {
        return -1;
    }
    const NavigationEntry& current = entries[static_cast<size_t>(currentEntry)];
    if (viewMode == ItemViewMode::List) {
        if (direction == NavigationDirection::Up && currentEntry > 0) {
            return currentEntry - 1;
        }
        if (direction == NavigationDirection::Down && currentEntry + 1 < static_cast<int>(entries.size())) {
            return currentEntry + 1;
        }
        return -1;
    }

    if (direction == NavigationDirection::Left) {
        for (int i = currentEntry - 1; i >= 0; --i) {
            if (entries[static_cast<size_t>(i)].row != current.row) break;
            if (entries[static_cast<size_t>(i)].column < current.column) return i;
        }
    } else if (direction == NavigationDirection::Right) {
        for (int i = currentEntry + 1; i < static_cast<int>(entries.size()); ++i) {
            if (entries[static_cast<size_t>(i)].row != current.row) break;
            if (entries[static_cast<size_t>(i)].column > current.column) return i;
        }
        if (currentEntry + 1 < static_cast<int>(entries.size())) return currentEntry + 1;
    } else if (direction == NavigationDirection::Up) {
        return closestEntryInRow(entries, current.row - 1, current.column);
    } else if (direction == NavigationDirection::Down) {
        return closestEntryInRow(entries, current.row + 1, current.column);
    }
    return -1;
}

} // namespace launcher::main_dock

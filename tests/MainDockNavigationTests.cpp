#include "ui/dock/MainDockNavigation.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

launcher::LaunchItem item(std::string id, std::string name, launcher::LaunchItemType type = launcher::LaunchItemType::App)
{
    launcher::LaunchItem value;
    value.id = std::move(id);
    value.name = std::move(name);
    value.type = type;
    return value;
}

} // namespace

int main()
{
    using launcher::main_dock::NavigationDirection;
    using launcher::main_dock::NavigationEntry;

    bool ok = true;
    const std::vector<launcher::LaunchItem> items = {item("b", "Beta"), item("a", "Alpha"), item("c", "Charlie"), item("d", "Delta")};
    const std::vector<NavigationEntry> sorted =
        launcher::main_dock::buildNavigationEntries(items, launcher::ItemViewMode::Icon, launcher::SortMode::Name, 48, 300.0f);
    ok &= require(sorted.size() == 4 && sorted[0].itemIndex == 1 && sorted[1].itemIndex == 0,
                  "navigation entries follow the configured sort order");
    ok &= require(sorted[0].row == 0 && sorted[2].column == 2 && sorted[3].row == 1,
                  "icon navigation entries are arranged into rows and columns");
    ok &= require(launcher::main_dock::findNavigationTarget(sorted, 2, launcher::ItemViewMode::Icon, NavigationDirection::Right) == 3,
                  "right navigation wraps to the next row");
    ok &= require(launcher::main_dock::findNavigationTarget(sorted, 1, launcher::ItemViewMode::Icon, NavigationDirection::Down) == 3,
                  "vertical navigation selects the closest column");

    const std::vector<NavigationEntry> list =
        launcher::main_dock::buildNavigationEntries(items, launcher::ItemViewMode::List, launcher::SortMode::Free, 48, 300.0f);
    ok &= require(launcher::main_dock::findNavigationTarget(list, 1, launcher::ItemViewMode::List, NavigationDirection::Up) == 0 &&
                      launcher::main_dock::findNavigationTarget(list, 1, launcher::ItemViewMode::List, NavigationDirection::Down) == 2,
                  "list navigation moves one row at a time");

    const std::vector<launcher::LaunchItem> titled = {item("title", "Group", launcher::LaunchItemType::Title), item("app", "App")};
    const std::vector<NavigationEntry> titleEntries =
        launcher::main_dock::buildNavigationEntries(titled, launcher::ItemViewMode::Icon, launcher::SortMode::Free, 48, 300.0f);
    ok &= require(titleEntries[0].row == 0 && titleEntries[1].row == 1, "title items occupy their own navigation row");

    return ok ? 0 : 1;
}

#pragma once

#include "launcher/Models.hpp"
#include "ui/common/UiTheme.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace launcher {

struct AppContext;

struct ItemEditorState {
    bool* showItemEditor = nullptr;
    bool* openItemEditorPopup = nullptr;
    int* editingCategory = nullptr;
    std::string* editingFolderId = nullptr;
    int* editingItem = nullptr;
    LaunchItem* editingDraft = nullptr;
    std::string* editingTarget = nullptr;
    std::string* editingStartDir = nullptr;
    std::string* editingRemark = nullptr;
    std::string* editingIcon = nullptr;
};

struct ItemEditorApi {
    std::vector<LaunchItem>* (*editingItems)(AppContext&) = nullptr;
    void (*selectSingle)(AppContext&, const LaunchItem&) = nullptr;
    std::int64_t (*nowUnix)() = nullptr;
};

void drawItemEditor(const UiPalette& theme, AppContext& context, ItemEditorState state, const ItemEditorApi& api);

} // namespace launcher

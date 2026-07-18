#pragma once

#include "ui/common/UiTheme.hpp"

#include <string>
#include <vector>

namespace launcher {

struct AppContext;

void drawNotesPanel(AppContext& context, const UiPalette& theme);
bool addNoteIdsAsListItems(AppContext& context, const std::vector<std::string>& noteIds);
std::vector<std::string> activeDragNoteIdsForDrop();

} // namespace launcher

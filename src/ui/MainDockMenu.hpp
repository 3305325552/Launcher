#pragma once

#include "ui/UiTheme.hpp"

namespace launcher {

struct AppContext;

void setMenuShortcutHintsVisible(bool visible);
bool menuItem(const UiPalette& theme, const char* icon, const char* label, const char* shortcut = nullptr, bool selected = false,
              bool enabled = true);
bool menuToggleItem(const UiPalette& theme, const char* icon, const char* label, bool selected, bool enabled = true);
bool beginIconMenu(const UiPalette& theme, const char* icon, const char* label);
void endIconMenu();
void drawViewMenu(const UiPalette& theme, AppContext& context);
void drawSortMenu(const UiPalette& theme, AppContext& context);

} // namespace launcher

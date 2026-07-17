#pragma once

#include "ui/UiTheme.hpp"

namespace launcher {

struct AppContext;

void drawUserGuideInline(const UiPalette& theme);
void drawUserGuideWindow(AppContext& context, const UiPalette& theme);

} // namespace launcher

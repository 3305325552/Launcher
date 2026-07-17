#pragma once

namespace launcher {

struct AppContext;

void drawSettingsPanel(AppContext& context);
void requestSettingsTab(AppContext& context, int tab);

} // namespace launcher

#pragma once

#include <string>

struct ID3D11Device;

namespace launcher {

struct AppSettings;
struct AppContext;

void setMainDockDevice(ID3D11Device* device);
void releaseMainDockCaches();
void releaseMainDockBackgroundCache();
void openMainDockSearch(AppContext& context);
void openMainDockSearchWithText(AppContext& context, std::string text);
void closeMainDockSearch(AppContext& context);
void closeMainDockWindows(AppContext& context);
void resetMainDockScrollOnNextFrame();
void drawMainDock(AppContext& context);

} // namespace launcher

#pragma once

#include <d3d11.h>
#include <string>

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

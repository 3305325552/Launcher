#pragma once

#include <string>

namespace launcher {

struct AppContext;

std::string configPathText(const AppContext& context);
std::string configDirectoryText(const AppContext& context);
void openConfigLocation(AppContext& context);
void changeConfigDirectory(AppContext& context);
void exportConfig(AppContext& context);
void importConfig(AppContext& context);

} // namespace launcher

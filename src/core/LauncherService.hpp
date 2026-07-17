#pragma once

#include "launcher/Models.hpp"

namespace launcher {

class LauncherService {
public:
    bool launch(const LaunchItem& item, int showCommand = 1) const;
};

} // namespace launcher

#include "data/SeedData.hpp"

namespace launcher {

AppState makeSeedState()
{
    AppState state;
    state.persisted().categories = {{"default", "Default", "home", {}}};
    return state;
}

} // namespace launcher

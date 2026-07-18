#pragma once

#include <functional>

namespace launcher::main_dock {

// MainDock binds feature views to non-owning functions implemented by its
// coordinator. Function pointers keep this contract explicit and avoid type
// erasure for callbacks that never capture state.
template<typename Signature> using Callback = Signature*;

using DeferredCallback = std::function<void()>;

} // namespace launcher::main_dock

#include "loading_events.h"

#include <chrono>

#include "input.h"

namespace loading
{

auto pump_events() -> void
{
    using clock = std::chrono::steady_clock;
    constexpr auto interval = std::chrono::milliseconds( 16 );
    static auto last_pump = clock::time_point{};
    const auto now = clock::now();
    if( now - last_pump < interval ) {
        return;
    }
    last_pump = now;
    inp_mngr.pump_events();
}

} // namespace loading

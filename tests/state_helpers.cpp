#include "state_helpers.h"

#include "../src/map/map.h"
#include "calendar.h"
#include "cata_arena.h"
#include "map_helpers.h"
#include "name.h"
#include "player_helpers.h"
#include "weather/weather.h"

namespace {

auto full_test_state() -> enum_bitset<test_state> {
    return state::avatar | state::creature | state::npc | state::vehicle | state::map | state::time
         | state::name | state::arena | state::weather;
}

} // namespace

auto clear_states(const enum_bitset<test_state>& states) -> void {
    auto normalized_states = states;

    disable_mapgen = true;

    if (normalized_states.test(state::npc)) { normalized_states.set(state::avatar); }

    if (normalized_states.test(state::weather)) {
        // A fresh manager also drops the humidity and wind of the last weather update.
        auto& weather = get_weather();
        weather = weather_manager{};
        weather.weather_id = weather_type_id("clear");
    }

    // clear_avatar marks skills as last practiced at the current turn, so move the turn first.
    if (normalized_states.test(state::time)) { calendar::turn = calendar::turn_zero; }
    if (normalized_states.test(state::avatar)) { clear_avatar(); }

    if (normalized_states.test(state::map)) {
        clear_map();
    } else {
        if (normalized_states.test(state::npc)) { clear_npcs(); }
        if (normalized_states.test(state::creature)) { clear_creatures(); }
        if (normalized_states.test(state::vehicle)) { clear_vehicles(); }
    }

    if (normalized_states.test(state::time)) { set_time(calendar::turn_zero); }
    if (normalized_states.test(state::name)) { Name::clear(); }
    if (normalized_states.test(state::arena)) { cleanup_arenas(); }
}

auto clear_all_state() -> void { clear_states(full_test_state()); }

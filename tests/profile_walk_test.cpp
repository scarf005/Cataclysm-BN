#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catch/catch.hpp"
#if __has_include("fov_z_range.h")
#    include "fov_z_range.h"
#    define PROFILE_HAS_RANGE 1
#endif
#include "game.h"
#include "map/map.h"
#include "map_helpers.h"
#include "monster.h"
#include "options_helpers.h"
#include "state_helpers.h"
#include "weather/weather.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

/// Walks the avatar east for PROFILE_WALK_STEPS turns (default 300) through a street of
/// buildings with monsters around, timing each move plus do_turn. Profile-only.
TEST_CASE("profile_walk", "[.][profile]") {
    const auto steps =
        std::getenv("PROFILE_WALK_STEPS") ? std::atoi(std::getenv("PROFILE_WALK_STEPS")) : 300;
    const auto no_autosave = override_option("AUTOSAVE", "false");
    // Monsters spawned in newly loaded submaps would make safe mode refuse further steps.
    const auto no_safemode = override_option("SAFEMODE", "false");
#if defined(PROFILE_HAS_RANGE)
    const auto range = fov_3d_z_range;
#else
    const auto range = -1;
#endif
    clear_all_state();
    g->place_player(tripoint_bub_ms(30, 66, 0));
    get_weather().weather_id = weather_type_id("clear");
    calendar::turn = calendar::turn_zero + 12_hours;
    g->reset_light_level();

    auto& here = get_map();
    const auto wall = ter_id("t_brick_wall");
    const auto floor = ter_id("t_floor");
    const auto roof = ter_id("t_flat_roof");
    // Two-storey houses on both sides of the street and a few monsters in the bubble.
    for (auto x = 0; x < MAPSIZE_X; ++x) {
        for (const auto side : {-1, 1}) {
            for (auto d = 3; d <= 10; ++d) {
                const auto y = 66 + side * d;
                for (auto z = 0; z <= 2; ++z) {
                    const auto edge = d == 3 || d == 10 || x % 12 == 0;
                    const auto door = d == 3 && x % 12 == 6 && z == 0;
                    here.ter_set(
                        tripoint_bub_ms(x, y, z),
                        z == 2 ? roof
                        : edge && !door
                            ? wall
                            : floor);
                }
            }
        }
    }
    for (auto i = 0; i < 12; ++i) {
        spawn_test_monster("mon_zombie", tripoint_bub_ms(20 + i * 8, 66 + (i % 2 ? 2 : -2), 0))
            .friendly = -1;
    }

    const auto start = std::chrono::steady_clock::now();
    auto moved = 0;
    for (auto i = 0; i < steps; ++i) {
        // Map shifts bring in new terrain; keep the next tile passable so every turn is a step.
        const auto next = g->u.bub_pos() + tripoint_rel_ms::east();
        here.ter_set(next, floor);
        here.furn_set(next, furn_str_id::NULL_ID());
        // Stamina runs out after ~400 steps and leaves the avatar without moves for whole turns.
        g->u.set_stamina(g->u.get_stamina_max());
        g->safe_mode = SAFE_MODE_OFF;
        moved += avatar_action::move(g->u, here, tripoint_rel_ms::east()) ? 1 : 0;
        // Leftover moves would make do_turn read input through the UI, which tests do not have.
        g->u.moves = std::min(g->u.moves, 0);
        REQUIRE_FALSE(g->do_turn());
    }
    const auto ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("PROFILE walk steps=%d moved=%d range=%d total_ms=%.1f per_step_ms=%.3f\n", steps,
                moved, range, ms, ms / steps);
}

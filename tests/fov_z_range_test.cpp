#include "calendar.h"
#include "cata_utility.h"
#include "catch/catch.hpp"
#include "fov_z_range.h"
#include "game.h"
#include "game_constants.h"
#include "map/lightmap.h"
#include "map/map.h"
#include "state_helpers.h"
#include "weather/weather.h"

#include <algorithm>
#include <ranges>
#include <vector>

namespace {

/// Vision caches of one z-level after a rebuild.
struct level_vision {
    int z;
    std::vector<float> seen;
    std::vector<lit_level> visibility;
    bool lightmap_dirty;
    bool absorption_dirty;
};

/// Rebuilds every map cache at the player's level with @p range and returns each level's result.
auto rebuild_vision(const int range) -> std::vector<level_vision> {
    namespace ranges = std::ranges;
    using namespace std::views;

    fov_3d_z_range = range;
    auto& here = get_map();
    const auto all_levels = iota(-OVERMAP_DEPTH, OVERMAP_HEIGHT + 1);
    // Run twice: the first pass adapts the player's vision threshold to the current light.
    for (auto pass = 0; pass < 2; ++pass) {
        ranges::for_each(all_levels, [&](const int z) { here.invalidate_map_cache(z); });
        here.build_map_cache(0);
        here.update_visibility_cache(0);
    }
    return all_levels | transform([&](const int z) {
               const auto& cache = here.access_cache(z);
               return level_vision{
                   .z = z,
                   .seen = cache.seen_cache,
                   .visibility = cache.visibility_cache,
                   .lightmap_dirty = cache.lightmap_dirty,
                   .absorption_dirty = cache.absorption_cache_dirty.any(),
               };
           })
           | ranges::to<std::vector>();
}

auto at_z(const std::vector<level_vision>& levels, const int z) -> const level_vision& {
    return levels[z + OVERMAP_DEPTH];
}

auto setup_daylight() -> void {
    clear_all_state();
    g->place_player(tripoint_bub_ms(60, 60, 0));
    get_weather().weather_id = weather_type_id("clear");
    calendar::turn = calendar::turn_zero + 12_hours;
    g->reset_light_level();
}

} // namespace

TEST_CASE("fov_z_range_limits_vision_levels", "[vision][fov_z_range]") {
    const auto restore_range = restore_on_out_of_scope<int>(fov_3d_z_range);
    setup_daylight();

    const auto full = rebuild_vision(OVERMAP_LAYERS);
    const auto limited = rebuild_vision(1);

    SECTION("levels outside the range are not seen") {
        for (const auto z : {-OVERMAP_DEPTH, -2, 2, 3, OVERMAP_HEIGHT}) {
            CAPTURE(z);
            const auto& level = at_z(limited, z);
            CHECK(std::ranges::all_of(level.visibility, [](const lit_level ll) {
                return ll == lit_level::BLANK;
            }));
            CHECK(std::ranges::all_of(level.seen, [](const float s) {
                return s == LIGHT_TRANSPARENCY_SOLID;
            }));
        }
    }

    SECTION("levels outside the range keep dirty lightmap and absorption until in range") {
        CHECK(at_z(limited, 3).lightmap_dirty);
        CHECK(at_z(limited, 3).absorption_dirty);
        CHECK_FALSE(at_z(limited, 1).lightmap_dirty);
        CHECK_FALSE(at_z(limited, 1).absorption_dirty);
    }

    SECTION("levels inside the range are computed as with the full range") {
        for (const auto z : {-1, 0, 1}) {
            CAPTURE(z);
            CHECK(at_z(limited, z).seen == at_z(full, z).seen);
            CHECK(at_z(limited, z).visibility == at_z(full, z).visibility);
        }
        // Sanity check: open air above the player is lit at midday.
        CHECK(std::ranges::any_of(at_z(limited, 1).visibility, [](const lit_level ll) {
            return ll != lit_level::BLANK;
        }));
    }
}

TEST_CASE("fov_z_range_covering_all_levels_keeps_full_vision", "[vision][fov_z_range]") {
    const auto restore_range = restore_on_out_of_scope<int>(fov_3d_z_range);
    setup_daylight();

    const auto full = rebuild_vision(OVERMAP_LAYERS);
    const auto ten = rebuild_vision(10);

    for (const auto z : std::views::iota(-OVERMAP_DEPTH, OVERMAP_HEIGHT + 1)) {
        CAPTURE(z);
        CHECK(at_z(ten, z).seen == at_z(full, z).seen);
        CHECK(at_z(ten, z).visibility == at_z(full, z).visibility);
    }
}

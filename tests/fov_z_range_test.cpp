#include "calendar.h"
#include "cata_utility.h"
#include "catch/catch.hpp"
#include "fov_z_range.h"
#include "game.h"
#include "game_constants.h"
#include "map/lightmap.h"
#include "map/map.h"
#include "preload_config.h"
#include "state_helpers.h"
#include "weather/weather.h"

#include <algorithm>
#include <format>
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

/// Forces the CPU compute path, the only one FOV_3D_Z_RANGE limits, and restores the range and
/// the backend on exit.
struct cpu_vision_scope {
    restore_on_out_of_scope<int> range{fov_3d_z_range};
    preload_config::compute_accel accel = preload_config::get_compute_accel();
    cpu_vision_scope() { preload_config::set_compute_accel(preload_config::compute_accel::cpu); }
    ~cpu_vision_scope() { preload_config::set_compute_accel(accel); }
};

auto setup_daylight() -> void {
    clear_all_state();
    g->place_player(tripoint_bub_ms(60, 60, 0));
    get_weather().weather_id = weather_type_id("clear");
    calendar::turn = calendar::turn_zero + 12_hours;
    g->reset_light_level();
}

/// Builds a tower around the player's column from z=0: walls with a gap that shifts per storey,
/// floors with a shaft, and a flat roof at @p roof_z.
auto build_tower(const int roof_z) -> void {
    const auto t_brick_wall = ter_id("t_brick_wall");
    const auto t_floor = ter_id("t_floor");
    const auto t_flat_roof = ter_id("t_flat_roof");
    const auto t_open_air = ter_id("t_open_air");
    constexpr auto radius = 6;
    auto& here = get_map();
    for (auto z = 0; z <= roof_z; ++z) {
        for (auto dx = -radius; dx <= radius; ++dx) {
            for (auto dy = -radius; dy <= radius; ++dy) {
                const auto wall =
                    std::max(std::abs(dx), std::abs(dy)) == radius && !(dx == radius && dy == z);
                const auto shaft = z > 0 && dx == 2 && dy == 2;
                const auto ter =
                    z == roof_z ? t_flat_roof
                    : wall      ? t_brick_wall
                    : shaft     ? t_open_air
                                : t_floor;
                here.ter_set(tripoint_bub_ms(60 + dx, 60 + dy, z), ter);
            }
        }
    }
}

} // namespace

TEST_CASE("fov_z_range_limits_vision_levels", "[vision][fov_z_range]") {
    const auto scope = cpu_vision_scope{};
    setup_daylight();
    // Walls and floors on both sides of the range edge make the 3D cast split there.
    build_tower(3);

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

TEST_CASE("fov_z_range_vision_rebuild_benchmark", "[.][benchmark][vision]") {
    const auto scope = cpu_vision_scope{};
    setup_daylight();
    auto& here = get_map();

    const auto bench = [&](const std::string& scene, const int player_z) {
        g->place_player(tripoint_bub_ms(60, 60, player_z));
        for (const auto range : {OVERMAP_LAYERS, 5, 1}) {
            fov_3d_z_range = range;
            const auto suffix = std::format("{} z={} range={}", scene, player_z, range);
            BENCHMARK_ADVANCED("full rebuild " + suffix)(Catch::Benchmark::Chronometer meter) {
                meter.measure([&] {
                    for (const auto z : std::views::iota(-OVERMAP_DEPTH, OVERMAP_HEIGHT + 1)) {
                        here.invalidate_map_cache(z);
                    }
                    here.build_map_cache(player_z);
                    here.update_visibility_cache(player_z);
                });
            };
            BENCHMARK_ADVANCED("seen rebuild " + suffix)(Catch::Benchmark::Chronometer meter) {
                meter.measure([&] {
                    here.set_seen_cache_dirty(player_z);
                    here.build_map_cache(player_z, true);
                });
            };
            BENCHMARK_ADVANCED("absorption rebuild " + suffix)(
                Catch::Benchmark::Chronometer meter) {
                meter.measure([&] {
                    for (const auto z : std::views::iota(-OVERMAP_DEPTH, OVERMAP_HEIGHT + 1)) {
                        here.set_absorption_cache_dirty(z);
                    }
                    here.build_map_cache(player_z, true);
                });
            };
        }
    };

    bench("field", 0);
    build_tower(8);
    bench("tower", 0);
    bench("tower", 3);
}

#include "../src/map/map.h"
#include "avatar.h"
#include "catch/catch.hpp"
#include "game.h"
#include "map/field_type.h"
#include "map_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

struct heat_scene {
    const char* name;
    const char* terrain;
    /// Fire intensity on every tile within 8 tiles of the avatar; 0 places no fire.
    int fire_intensity;
};

auto env_or(const char* name, const std::string& fallback) -> std::string {
    const auto* value = std::getenv(name);
    return value ? value : fallback;
}

} // namespace

/// Times get_heat_radiation, which scans 13x13 tiles around the avatar for fire and hot terrain.
/// Each scene fills the map with one terrain and puts fd_fire on every tile within 8 tiles of the
/// avatar, then calls get_heat_radiation PROFILE_HEAT_CALLS times (default 200000) for both
/// `direct` values without advancing game time, so the fire neither burns out nor spreads.
/// PROFILE_HEAT_SCENE=fire|lava_fire|none runs one scene. Prints one line per scene with the time
/// per call and a checksum of the results.
TEST_CASE("heat radiation timing", "[.][benchmark]") {
    const auto calls = std::stoi(env_or("PROFILE_HEAT_CALLS", "200000"));
    const auto only_scene = env_or("PROFILE_HEAT_SCENE", "");
    for (const auto& scene :
         {heat_scene{.name = "fire", .terrain = "t_dirt", .fire_intensity = 1},
          heat_scene{.name = "lava_fire", .terrain = "t_lava", .fire_intensity = 1},
          heat_scene{.name = "none", .terrain = "t_dirt", .fire_intensity = 0}}) {
        if (!only_scene.empty() && only_scene != scene.name) { continue; }
        clear_all_state();
        build_test_map(ter_id(scene.terrain));
        auto& here = get_map();
        const auto pos = tripoint_bub_ms{60, 60, 0};
        get_avatar().setpos(pos);
        if (scene.fire_intensity > 0) {
            for (const auto& p : here.points_in_radius(pos, 8)) {
                REQUIRE(here.add_field(p, fd_fire, scene.fire_intensity, 0_turns, false));
            }
        }
        here.invalidate_map_cache(0);
        here.build_map_cache(0, true);
        auto fires = 0;
        for (const auto& p : here.points_in_radius(pos, 8)) {
            if (here.get_field(p, fd_fire) != nullptr) { ++fires; }
        }
        REQUIRE(fires == (scene.fire_intensity > 0 ? 17 * 17 : 0));

        auto checksum = std::int64_t{0};
        const auto start = std::chrono::steady_clock::now();
        for (auto i = 0; i < calls; ++i) { checksum += get_heat_radiation(pos, i % 2 == 0); }
        const auto ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        std::printf("PROFILE heat scene=%s calls=%d total_ms=%.1f per_call_ns=%.1f checksum=%lld\n",
                    scene.name, calls, ms, ms * 1e6 / calls, static_cast<long long>(checksum));
    }
}

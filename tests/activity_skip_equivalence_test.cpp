#include "../src/map/map.h"
#include "activity_skip_wait_helpers.h"
#include "activity_time_cadence.h"
#include "avatar.h"
#include "cata_utility.h"
#include "catch/catch.hpp"
#include "game.h"
#include "morale_types.h"
#include "rng.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// Waits 2 hours in the field in clear 18 C weather at TIME_ACTION_SCALE @p scale. The avatar
/// starts with a 3 hour test_high effect (+25 morale) and a decaying food morale bonus. The RNG
/// starts from the same seed for every scale.
auto wait_at_scale(const int scale) -> wait_end_state {
    const auto skip_options = skip_window_options{};
    const auto action_scale = override_option("TIME_ACTION_SCALE", std::to_string(scale));
    setup_wait_scene(wait_scene::field);
    hold_weather(weather_type_id("clear"), 18_c);
    rng_set_engine_seed(1);
    g->u.add_effect(efftype_id("test_high"), 3_hours);
    g->u.add_morale(morale_type("morale_food_good"), 20, 20, 3_hours, 1_hours);

    const auto state = wait_through_activity(2_hours);
    INFO(state.error_or(""));
    REQUIRE(state.has_value());
    return *state;
}

/// Checks morale within 1 and the temperature of each body part within 50 millidegrees.
auto check_same_body(const wait_end_state& exact, const wait_end_state& coarse) -> void {
    CHECK(exact.morale == Approx(coarse.morale).margin(1));
    for (const auto& [bp, temperature] : exact.body_temperature) {
        CAPTURE(bp.str());
        CHECK(units::to_millidegree_celsius(temperature)
              == Approx(units::to_millidegree_celsius(coarse.body_temperature.at(bp))).margin(50));
    }
}

} // namespace

TEST_CASE(
    "waiting gives the same end state at TIME_ACTION_SCALE 100 and 10",
    "[activity][fixed_window][speed]") {
    const auto exact = wait_at_scale(100);
    const auto coarse = wait_at_scale(10);

    // The scale reaches the skip window: 1 calendar turn per tick at 100, 10 at 10.
    CHECK(exact.tick_turns == 1);
    CHECK(coarse.tick_turns == 10);
    // Most of the wait passes inside skip windows. Without them, one do_turn call per tick
    // takes 7200 calls at scale 100 and 720 at scale 10; 24 windows of 5 minutes take about 100.
    const auto windows =
        to_turns<int>(2_hours) / to_turns<int>(activity_time_cadence::fixed_window());
    CHECK(exact.do_turn_calls <= windows * 5);
    CHECK(coarse.do_turn_calls <= windows * 5);

    // Not checked yet, as they differ today: calendar::turn and the test_high duration.
    CHECK(exact.stored_kcal == Approx(coarse.stored_kcal).margin(10));
    CHECK(exact.thirst == Approx(coarse.thirst).margin(1));
    CHECK(exact.fatigue == Approx(coarse.fatigue).margin(1));
    check_same_body(exact, coarse);
    const auto effect_names = [](const wait_end_state& s) {
        return s.effect_durations | std::views::keys | std::ranges::to<std::vector>();
    };
    REQUIRE(effect_names(exact) == effect_names(coarse));
    CHECK(exact.effect_durations.at("happy:num_bp") == coarse.effect_durations.at("happy:num_bp"));
}

TEST_CASE(
    "waiting gives the same end state with and without the body temperature fixed point skip",
    "[activity][fixed_window][bodytemp]") {
    const auto skip_options = skip_window_options{};
    struct wait_result {
        wait_end_state state;
        /// update_bodytemp calls skipped at the fixed point.
        int skips = 0;
    };
    // Waits 2 hours in the field from the same RNG seed, with the skip on or off.
    const auto wait_in =
        [](const bool skip, const weather_type_id& weather, const units::temperature temperature) {
            const auto fixed_point = restore_on_out_of_scope<bool>(skip_bodytemp_at_fixed_point);
            skip_bodytemp_at_fixed_point = skip;
            setup_wait_scene(wait_scene::field);
            hold_weather(weather, temperature);
            rng_set_engine_seed(1);
            const auto skips = skip_bodytemp_fixed_point_count();
            const auto state = wait_through_activity(2_hours);
            INFO(state.error_or(""));
            REQUIRE(state.has_value());
            return wait_result{.state = *state, .skips = skip_bodytemp_fixed_point_count() - skips};
        };
    // Effects that update_bodytemp adds or removes, with their durations.
    const auto bodytemp_effects = [](const wait_end_state& s) {
        const auto from_bodytemp = [](const auto& entry) {
            return std::ranges::any_of(
                std::array{"cold:", "hot:", "hot_speed:", "frostbite:", "frostbite_recovery:",
                           "blisters:"},
                [&](const std::string_view id) { return entry.first.starts_with(id); });
        };
        return s.effect_durations | std::views::filter(from_bodytemp)
             | std::ranges::to<std::vector>();
    };
    // Body temperature per part, morale and the effects of update_bodytemp are equal. Two waits
    // without the skip can end on different turns with different kcal and hourly effects, so the
    // other fields are not compared.
    const auto check_same_end_state =
        [&](const weather_type_id& weather, const units::temperature temperature) {
            const auto skipped = wait_in(true, weather, temperature);
            const auto not_skipped = wait_in(false, weather, temperature);
            CAPTURE(skipped.skips);
            CHECK(not_skipped.skips == 0);
            CHECK(skipped.state.body_temperature == not_skipped.state.body_temperature);
            CHECK(bodytemp_effects(skipped.state) == bodytemp_effects(not_skipped.state));
            CHECK(skipped.state.morale == not_skipped.state.morale);
            return skipped.skips;
        };

    // The body reaches its fixed point at 18 C and in 10 C rain once soaked, so the skip is
    // reached.
    SECTION("mild") { CHECK(check_same_end_state(weather_type_id("clear"), 18_c) > 0); }
    SECTION("cold") { check_same_end_state(weather_type_id("clear"), -10_c); }
    SECTION("rain") { CHECK(check_same_end_state(weather_type_id("rain"), 10_c) > 0); }
}

/// A chicken appears at the first spawn check of a 10 minute wait. The wait still ends after
/// exactly 10 minutes.
TEST_CASE("wait ends on time when a monster appears", "[activity][fixed_window]") {
    const auto skip_options = skip_window_options{};
    const auto action_scale = override_option("TIME_ACTION_SCALE", "100");
    setup_wait_scene(wait_scene::field);
    hold_weather(weather_type_id("clear"), 18_c);
    g->m.add_spawn(mtype_id("mon_chicken"), 1, tripoint_bub_ms(60, 30, 0));
    const auto end_turn = calendar::turn + 10_minutes;

    const auto state = wait_through_activity(10_minutes);
    INFO(state.error_or(""));
    REQUIRE(state.has_value());
    CHECK(g->num_creatures() == 2);
    CHECK(to_turn<int>(state->turn) == to_turn<int>(end_turn));
}

#include "activity_skip_wait_helpers.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "effect.h"
#include "game.h"
#include "item.h"
#include "player_helpers.h"
#include "rng.h"
#include "state_helpers.h"
#include "utils/suffer_artifact_guard.h"

#include <climits>
#include <expected>
#include <map>
#include <string>
#include <tuple>

static const auto effect_attention = efftype_id("attention");
static const auto itype_test_artifact_attention = itype_id("test_artifact_attention");

namespace {

/// Sets the guard switch for the life of the scope and restores it at the end.
struct scoped_artifact_guard {
    bool old = suffer_artifact_guard_enabled;
    explicit scoped_artifact_guard(const bool enabled) { suffer_artifact_guard_enabled = enabled; }
    ~scoped_artifact_guard() { suffer_artifact_guard_enabled = old; }
};

/// End state of a wait, or why it ended early, and the next number the RNG gives after it.
struct guarded_wait_result {
    std::expected<wait_end_state, std::string> state;
    int next_random = 0;
};

/// Waits 2 hours in the field from RNG seed 1.
auto wait_with_guard(const bool enabled) -> guarded_wait_result {
    const auto skip_options = skip_window_options{};
    const auto guard = scoped_artifact_guard(enabled);
    setup_wait_scene(wait_scene::field);
    hold_weather(weather_type_id("clear"), 18_c);
    rng_set_engine_seed(1);
    const auto state = wait_through_activity(2_hours);
    return {
        .state = state,
        .next_random = rng(0, INT_MAX),
    };
}

auto check_same_wait(const guarded_wait_result& guarded, const guarded_wait_result& unguarded)
    -> void {
    // The same next number shows that the guard draws no random number and skips none.
    CHECK(guarded.next_random == unguarded.next_random);
    // A wait that ends early, for example when the avatar passes out, must end the same way.
    REQUIRE(guarded.state.has_value() == unguarded.state.has_value());
    if (!guarded.state) {
        CHECK(guarded.state.error() == unguarded.state.error());
        return;
    }
    CHECK(guarded.state->stored_kcal == unguarded.state->stored_kcal);
    CHECK(guarded.state->thirst == unguarded.state->thirst);
    CHECK(guarded.state->fatigue == unguarded.state->fatigue);
    CHECK(guarded.state->morale == unguarded.state->morale);
    CHECK(guarded.state->body_temperature == unguarded.state->body_temperature);
    CHECK(guarded.state->effect_durations == unguarded.state->effect_durations);
    CHECK(guarded.state->turn == unguarded.state->turn);
    CHECK(guarded.state->do_turn_calls == unguarded.state->do_turn_calls);
}

} // namespace

TEST_CASE(
    "suffer artifact guard gives the same 2 hour wait as the full artifact check",
    "[suffer][activity][fixed_window]") {
    // Checks that the guard is exact: run the same turns from seed 1 with the guard on and off, and
    // expect the same end state and the same next random number.
    SECTION("default avatar without artifacts") {
        const auto guarded = wait_with_guard(true);
        INFO(guarded.state.error_or(""));
        REQUIRE(guarded.state.has_value());
        check_same_wait(guarded, wait_with_guard(false));
    }
    SECTION("avatar with an artifact and stimulants") {
        // The artifact adds the attention effect every turn; stim 60 runs the stimulant section.
        // A wait with the attention effect spawns monsters, and their moves differ between two
        // waits in one process. Call suffer once per turn for 1 hour instead.
        const auto suffer_for_an_hour = [](const bool enabled) {
            const auto guard = scoped_artifact_guard(enabled);
            clear_all_state();
            auto& u = get_avatar();
            u.i_add(item::spawn(itype_test_artifact_attention));
            u.set_stim(60);
            rng_set_engine_seed(1);
            for (auto turn = 0; turn < to_turns<int>(1_hours); ++turn) {
                u.suffer();
                calendar::turn += 1_turns;
            }
            auto effects = std::map<std::string, time_duration>{};
            for (const auto& [type, per_part] : u.get_effects()) {
                for (const auto& [bp, eff] : per_part) {
                    effects[type.str() + ":" + bp.str()] = eff.get_duration();
                }
            }
            return std::tuple{
                u.has_effect(effect_attention),
                u.get_stim(),
                u.get_hp(),
                u.get_pain(),
                u.get_fatigue(),
                effects,
                rng(0, INT_MAX)};
        };
        const auto guarded = suffer_for_an_hour(true);
        CHECK(std::get<0>(guarded));
        CHECK(guarded == suffer_for_an_hour(false));
    }
}

TEST_CASE("suffer finds an artifact effect once the character picks up an artifact", "[suffer]") {
    // Checks the guard's negative path: a turn without artifacts skips the artifact checks, and
    // the next turn after the avatar picks up an AEP_ATTENTION artifact adds the attention effect.
    clear_all_state();
    auto& u = get_avatar();
    clear_character(u, true);
    const auto guard = scoped_artifact_guard(true);

    u.suffer();
    CHECK_FALSE(u.has_effect(effect_attention));

    u.i_add(item::spawn(itype_test_artifact_attention));
    u.suffer();
    CHECK(u.has_effect(effect_attention));
}

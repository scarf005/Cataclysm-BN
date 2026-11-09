#include "avatar.h"
#include "catch/catch.hpp"
#include "game.h"
#include "input_pending.h"
#include "options_helpers.h"
#include "player_activity.h"
#include "state_helpers.h"
#include "type_id.h"

// Only the branches that return before polling the platform input queue are covered here:
// polling needs an initialized window, so the queued-key path is verified manually.

TEST_CASE("queued_input_redraw_scope_nests", "[input][redraw]") {
    CHECK_FALSE(queued_input_redraw_scope::active());
    {
        const auto outer = queued_input_redraw_scope{};
        CHECK(queued_input_redraw_scope::active());
        {
            const auto inner = queued_input_redraw_scope{};
            CHECK(queued_input_redraw_scope::active());
        }
        CHECK(queued_input_redraw_scope::active());
    }
    CHECK_FALSE(queued_input_redraw_scope::active());
}

TEST_CASE("redraw_is_not_deferred_when_disallowed", "[input][redraw]") {
    clear_all_state();
    const auto restore_safe_mode = g->safe_mode;

    SECTION("option disabled") {
        const auto opt = override_option("DEFER_REDRAW_ON_QUEUED_INPUT", "false");
        CHECK_FALSE(should_defer_redraw_for_queued_input());
    }

    SECTION("safe mode stopped the player") {
        const auto opt = override_option("DEFER_REDRAW_ON_QUEUED_INPUT", "true");
        g->safe_mode = SAFE_MODE_STOP;
        CHECK_FALSE(should_defer_redraw_for_queued_input());
    }

    SECTION("death cam") {
        const auto opt = override_option("DEFER_REDRAW_ON_QUEUED_INPUT", "true");
        g->uquit = QUIT_WATCH;
        CHECK_FALSE(should_defer_redraw_for_queued_input());
        g->uquit = QUIT_NO;
    }

    SECTION("activity in progress") {
        const auto opt = override_option("DEFER_REDRAW_ON_QUEUED_INPUT", "true");
        get_avatar().assign_activity(activity_id("ACT_WAIT"), 1000, 0);
        REQUIRE(get_avatar().activity);
        CHECK_FALSE(should_defer_redraw_for_queued_input());
        get_avatar().cancel_activity();
    }

    g->safe_mode = restore_safe_mode;
}

#include "cata_utility.h"
#include "catch/catch.hpp"
#include "creature_tracker.h"
#include "game.h"
#include "map_helpers.h"
#include "monster.h"
#include "state_helpers.h"

#include <array>
#include <barrier>
#include <thread>

TEST_CASE("parallel creature lookups preserve tracked monsters", "[creature_tracker][parallel]") {
    clear_all_state();
    const auto cleanup = on_out_of_scope([]() { clear_all_state(); });
    move_player_out_of_the_way();

    const auto pos = tripoint_bub_ms(60, 60, 0);
    auto& mon = spawn_test_monster("mon_test_bash", pos);
    const auto abs_pos = mon.abs_pos();
    const auto owner = g->critter_tracker->find(pos);
    REQUIRE(owner.get() == &mon);
    const auto owners_before = owner.use_count();
    const auto absolute = GENERATE(false, true);

    auto start = std::barrier(2);
    auto results = std::array<monster*, 2>{};
    const auto lookup = [&](const auto index) {
        start.arrive_and_wait();
        results[index] = absolute ? g->critter_at<monster>(abs_pos) : g->critter_at<monster>(pos);
    };
    {
        auto first = std::jthread(lookup, 0);
        auto second = std::jthread(lookup, 1);
    }

    CHECK(results[0] == &mon);
    CHECK(results[1] == &mon);
    CHECK(owner.use_count() == owners_before);
    mon.setpos(pos + tripoint_rel_ms(1, 0, 0));
    CHECK(g->critter_at<monster>(mon.bub_pos()) == &mon);
    CHECK(g->critter_at<monster>(pos) == nullptr);
}

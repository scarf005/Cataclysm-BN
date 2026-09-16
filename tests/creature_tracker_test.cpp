#include "avatar.h"
#include "cata_utility.h"
#include "catch/catch.hpp"
#include "creature_tracker.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "mapbuffer.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "state_helpers.h"

#include <array>
#include <latch>
#include <thread>

namespace {

/// Two unordered reads are enough for TSan to detect refcount writes; no timing or stress loop.
auto lookup_concurrently(const auto& lookup) -> std::array<decltype(lookup()), 2> {
    auto start = std::latch(2);
    auto results = std::array<decltype(lookup()), 2>{};
    const auto run = [&](const auto index) {
        start.arrive_and_wait();
        results[index] = lookup();
    };
    auto first = std::thread(run, 0);
    auto second = std::thread(run, 1);
    first.join();
    second.join();
    return results;
}

template <typename T>
auto lookup_creature(
    const tripoint_bub_ms& pos, const bool absolute,
    const bool allow_hallucination = false) -> const T* { // *NOPAD*
    const auto& game_state = *g;
    return absolute ? game_state.critter_at<T>(bub_to_abs(pos), allow_hallucination)
                    : game_state.critter_at<T>(pos, allow_hallucination);
}

} // namespace

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

    const auto results = lookup_concurrently([&]() {
        return absolute ? g->critter_at<monster>(abs_pos) : g->critter_at<monster>(pos);
    });
    CHECK(results[0] == &mon);
    CHECK(results[1] == &mon);
    CHECK(owner.use_count() == owners_before);

    const auto map_results = lookup_concurrently([&]() {
        return mon.get_mapbuffer().creature_at(abs_pos);
    });
    CHECK(map_results[0] == &mon);
    CHECK(map_results[1] == &mon);
    CHECK(owner.use_count() == owners_before);

    mon.setpos(pos + tripoint_rel_ms(1, 0, 0));
    CHECK(g->critter_at<monster>(mon.bub_pos()) == &mon);
    CHECK(g->critter_at<monster>(pos) == nullptr);
}

TEST_CASE("parallel creature lookups preserve tracked NPCs", "[creature_tracker][parallel][npc]") {
    clear_all_state();
    const auto cleanup = on_out_of_scope([]() { clear_all_state(); });
    move_player_out_of_the_way();

    const auto pos = tripoint_bub_ms(60, 60, 0);
    auto& guy = spawn_npc(pos, "test_talker");
    const auto abs_pos = guy.abs_pos();
    const auto owner = guy.get_mapbuffer().find_active_npc(abs_pos);
    REQUIRE(owner.get() == &guy);
    const auto owners_before = owner.use_count();
    const auto absolute = GENERATE(false, true);

    const auto results = lookup_concurrently([&]() {
        return absolute ? g->critter_at<Creature>(abs_pos) : g->critter_at<Creature>(pos);
    });
    CHECK(results[0] == &guy);
    CHECK(results[1] == &guy);
    CHECK(owner.use_count() == owners_before);

    const auto map_results = lookup_concurrently([&]() {
        return guy.get_mapbuffer().creature_at(abs_pos);
    });
    CHECK(map_results[0] == &guy);
    CHECK(map_results[1] == &guy);
    CHECK(owner.use_count() == owners_before);

    guy.setpos(pos + tripoint_rel_ms(1, 0, 0));
    CHECK(g->critter_at<npc>(guy.bub_pos()) == &guy);
    CHECK(g->critter_at<npc>(pos) == nullptr);
}

TEST_CASE(
    "parallel monster sight checks preserve their target", "[creature_tracker][parallel][vision]") {
    clear_all_state();
    const auto cleanup = on_out_of_scope([]() { clear_all_state(); });
    move_player_out_of_the_way();

    auto& observer = spawn_test_monster("mon_test_bash", tripoint_bub_ms(60, 60, 0));
    auto& target = spawn_test_monster("mon_test_bash", tripoint_bub_ms(65, 60, 0));
    const auto owner = g->critter_tracker->find(target.abs_pos());
    const auto owners_before = owner.use_count();

    // The fixture has no sight beyond this range. The visibility lookup still runs
    // before the range rejection, as it does when AI workers evaluate distant targets.
    const auto results = lookup_concurrently([&]() { return observer.sees(target.bub_pos()); });
    CHECK_FALSE(results[0]);
    CHECK_FALSE(results[1]);
    CHECK(owner.use_count() == owners_before);
    target.setpos(target.bub_pos() + tripoint_rel_ms(1, 0, 0));
    CHECK(g->critter_at<monster>(target.bub_pos()) == &target);
}

TEST_CASE("creature lookups preserve type and lifecycle filtering", "[creature_tracker]") {
    clear_all_state();
    const auto cleanup = on_out_of_scope([]() { clear_all_state(); });
    move_player_out_of_the_way();

    const auto pos = tripoint_bub_ms(60, 60, 0);
    const auto absolute = GENERATE(false, true);
    auto& buffer = g->u.get_mapbuffer();
    const auto abs_pos = bub_to_abs(pos);

    SECTION("empty tile") {
        CHECK(lookup_creature<Creature>(pos, absolute) == nullptr);
        CHECK(buffer.creature_at(abs_pos) == nullptr);
        CHECK(buffer.creature_tracker().find_borrowed(abs_pos) == nullptr);
        CHECK(buffer.find_active_npc_borrowed(abs_pos) == nullptr);
    }
    SECTION("live and dead monster") {
        auto& mon = spawn_test_monster("mon_test_bash", pos);
        CHECK(lookup_creature<Creature>(pos, absolute) == &mon);
        CHECK(lookup_creature<monster>(pos, absolute) == &mon);
        CHECK(lookup_creature<Character>(pos, absolute) == nullptr);
        CHECK(buffer.creature_tracker().find_borrowed(abs_pos) == &mon);
        mon.set_hp(0);
        REQUIRE(mon.is_dead());
        CHECK(lookup_creature<Creature>(pos, absolute) == nullptr);
        CHECK(buffer.creature_tracker().find_borrowed(abs_pos) == nullptr);
        CHECK(buffer.creature_tracker().find(abs_pos) == nullptr);
        CHECK(buffer.creature_at(abs_pos) == nullptr);
    }
    SECTION("hallucination filtering") {
        auto& mon = spawn_test_monster("mon_test_bash", pos);
        mon.hallucination = true;
        CHECK(lookup_creature<Creature>(pos, absolute) == nullptr);
        CHECK(lookup_creature<monster>(pos, absolute, true) == &mon);
        CHECK(buffer.creature_at(abs_pos) == nullptr);
        CHECK(buffer.creature_at(abs_pos, true) == &mon);
    }
    SECTION("avatar") {
        g->u.setpos(pos);
        CHECK(lookup_creature<Creature>(pos, absolute) == &g->u);
        CHECK(lookup_creature<Character>(pos, absolute) == &g->u);
        CHECK(lookup_creature<avatar>(pos, absolute) == &g->u);
        CHECK(lookup_creature<player>(pos, absolute) == &g->u);
        CHECK(lookup_creature<npc>(pos, absolute) == nullptr);
        CHECK(lookup_creature<monster>(pos, absolute) == nullptr);
        CHECK(buffer.creature_at(abs_pos) == &g->u);
    }
    SECTION("live and dead NPC") {
        auto& guy = spawn_npc(pos, "test_talker");
        CHECK(lookup_creature<Creature>(pos, absolute) == &guy);
        CHECK(lookup_creature<Character>(pos, absolute) == &guy);
        CHECK(lookup_creature<npc>(pos, absolute) == &guy);
        CHECK(lookup_creature<player>(pos, absolute) == &guy);
        CHECK(lookup_creature<monster>(pos, absolute) == nullptr);
        CHECK(buffer.find_active_npc_borrowed(abs_pos) == &guy);
        guy.set_part_hp_cur(bodypart_id("torso"), 0);
        REQUIRE(guy.is_dead());
        CHECK(lookup_creature<Creature>(pos, absolute) == nullptr);
        CHECK(buffer.find_active_npc_borrowed(abs_pos) == nullptr);
        CHECK(buffer.find_active_npc(abs_pos) == nullptr);
        CHECK(buffer.creature_at(abs_pos) == nullptr);
    }
    SECTION("mounted avatar") {
        auto& mon = spawn_test_monster("mon_test_bash", pos);
        g->u.mount_creature(mon);
        REQUIRE(g->u.bub_pos() == pos);
        CHECK(lookup_creature<Creature>(pos, absolute) == &mon);
        CHECK(lookup_creature<monster>(pos, absolute) == &mon);
        CHECK(lookup_creature<Character>(pos, absolute) == &g->u);
        CHECK(lookup_creature<avatar>(pos, absolute) == &g->u);
        CHECK(lookup_creature<player>(pos, absolute) == &g->u);
        CHECK(buffer.creature_at(abs_pos) == &mon);
    }
    SECTION("mounted NPC") {
        auto& mon = spawn_test_monster("mon_test_bash", pos);
        auto& guy = spawn_npc(pos + tripoint_rel_ms(1, 0, 0), "test_talker");
        guy.mount_creature(mon);
        REQUIRE(guy.bub_pos() == pos);
        CHECK(lookup_creature<Creature>(pos, absolute) == &mon);
        CHECK(lookup_creature<monster>(pos, absolute) == &mon);
        CHECK(lookup_creature<Character>(pos, absolute) == &guy);
        CHECK(lookup_creature<npc>(pos, absolute) == &guy);
        CHECK(lookup_creature<player>(pos, absolute) == &guy);
        CHECK(buffer.creature_at(abs_pos) == &mon);
    }
}

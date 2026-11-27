#include "../src/map/map.h"
#include "avatar.h"
#include "catalua_impl.h"
#include "catalua_sol.h"
#include "catch/catch.hpp"
#include "computer.h"
#include "computer_session.h"
#include "debug.h"
#include "event.h"
#include "event_bus.h"
#include "fstream_utils.h"
#include "game.h"
#include "init.h"
#include "json.h"
#include "line.h"
#include "map_helpers.h"
#include "monster.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace {

auto lua_state() -> sol::state& { return DynamicDataLoader::get_instance().lua->lua; }

auto computer_functions() -> sol::table {
    return lua_state()["game"]["computer_functions"].get<sol::table>();
}

auto computer_failure_functions() -> sol::table {
    return lua_state()["game"]["computer_failure_functions"].get<sol::table>();
}

/// Reads the action names in the "options" or "failures" list of a computer's JSON save.
auto saved_names(const computer& comp, const std::string& list) -> std::vector<std::string> {
    auto is = std::istringstream(serialize(comp));
    auto jsin = JsonIn(is);
    const auto jo = jsin.get_object();
    jo.allow_omitted_members();
    auto names = std::vector<std::string>{};
    for (const auto& entry : jo.get_array(list)) {
        const auto entry_jo = entry.get_object();
        entry_jo.allow_omitted_members();
        names.push_back(entry_jo.get_string("action"));
    }
    return names;
}

auto saved_actions(const computer& comp) -> std::vector<std::string> {
    return saved_names(comp, "options");
}

auto saved_security(const computer& comp) -> int {
    auto is = std::istringstream(serialize(comp));
    auto jsin = JsonIn(is);
    const auto jo = jsin.get_object();
    jo.allow_omitted_members();
    return jo.get_int("security");
}

/// Legacy save string with one option per legacy action number 0..49.
auto legacy_save_with_every_action() -> std::string {
    auto out = std::ostringstream{};
    out << "Old_Terminal 0 -1 50";
    for (auto i = 0; i < 50; i++) { out << " opt " << i << " 0"; }
    out << " 0";
    return out.str();
}

/// Legacy save string with no options and one failure per legacy failure number 0..10.
auto legacy_save_with_every_failure() -> std::string {
    auto out = std::ostringstream{};
    out << "Old_Terminal 0 -1 0 11";
    for (auto i = 0; i < 11; i++) { out << " " << i; }
    return out.str();
}

} // namespace

/// Old saves stored actions as numbers. Each number must load as the action name that
/// game.computer_functions uses, so old terminals still work after the move to Lua.
/// Number 15 was a removed missile option and is skipped.
TEST_CASE("computer_legacy_actions_resolve_to_lua_functions", "[computer][lua]") {
    auto comp = computer("Test", 0);
    comp.load_legacy_data(legacy_save_with_every_action());
    const auto actions = saved_actions(comp);

    REQUIRE(actions.size() == 49);
    CHECK(actions[0] == "null");
    CHECK(actions[3] == "unlock");
    CHECK(actions[15] == "miss_disarm");
    CHECK(actions[48] == "radio_archive");

    const auto functions = computer_functions();
    for (const auto& action : actions) {
        if (action == "null") { continue; }
        INFO(action);
        CHECK(functions[action].get_type() == sol::type::function);
    }
    CHECK(functions["unlock_labpass"].get_type() == sol::type::function);
}

/// The unlock action runs from data/json/lua/computer.lua on the real map and avatar.
/// A Lua table stands in for the session because the real one waits for a key press.
/// Locked doors within 8 tiles open; a door farther away stays locked.
TEST_CASE("computer_lua_unlock_opens_nearby_doors", "[computer][lua]") {
    clear_all_state();
    auto& here = get_map();
    auto& u = get_avatar();
    const auto near = u.bub_pos() + point(3, 0);
    const auto far = u.bub_pos() + point(10, 0);
    here.ter_set(near, ter_str_id("t_door_metal_locked"));
    here.ter_set(far, ter_str_id("t_door_metal_locked"));

    auto& lua = lua_state();
    auto shown = std::string{};
    auto session = lua.create_table();
    session["query_any"] = [&](const sol::table&, const std::string& text) { shown = text; };
    auto params = lua.create_table();
    params["session"] = session;
    params["user"] = &u;

    const auto res = computer_functions().get<sol::protected_function>("unlock")(params);
    REQUIRE(res.valid());

    CHECK(here.ter(near).id() == ter_str_id("t_door_metal_c"));
    CHECK(here.ter(far).id() == ter_str_id("t_door_metal_locked"));
    CHECK(shown == "Lock disabled.  Press any key…");
}

/// computer_session::activate_function looks up the action in game.computer_functions and
/// passes the real session and avatar. A probe function checks both and removes its option.
/// An action with no Lua function reports a debug message and costs no moves.
TEST_CASE("computer_session_runs_lua_action_with_real_session", "[computer][lua]") {
    clear_all_state();
    auto comp = computer("Test", 0);
    comp.add_option("Probe", "test_probe", 0);
    comp.add_option("Unlock", "unlock", 0);
    auto session = computer_session(comp);
    session.set_alerts(2);

    auto functions = computer_functions();
    auto seen_alerts = 0;
    auto seen_avatar = false;
    functions["test_probe"] = [&](const sol::table& params) {
        auto& s = params.get<computer_session&>("session");
        seen_alerts = s.get_alerts();
        seen_avatar = params.get<avatar*>("user") == &get_avatar();
        s.remove_option("test_probe");
    };

    session.activate_function("test_probe");
    functions["test_probe"] = sol::lua_nil;

    CHECK(seen_alerts == 2);
    CHECK(seen_avatar);
    CHECK(saved_actions(comp) == std::vector<std::string>{"unlock"});

    const auto moves = get_avatar().moves;
    const auto msg = capture_debugmsg_during([&] { session.activate_function("no_such_action"); });
    CHECK(msg.contains("no_such_action"));
    CHECK(get_avatar().moves == moves);
}

/// Old saves stored failures as numbers 0..10. Each number must load as a failure name that
/// game.computer_failure_functions uses, so old terminals still punish failed hacks.
TEST_CASE("computer_legacy_failures_resolve_to_lua_functions", "[computer][lua]") {
    auto comp = computer("Test", 0);
    comp.load_legacy_data(legacy_save_with_every_failure());
    const auto failures = saved_names(comp, "failures");

    REQUIRE(failures.size() == 11);
    CHECK(failures[0] == "null");
    CHECK(failures[1] == "shutdown");
    CHECK(failures[10] == "destroy_data");

    const auto functions = computer_failure_functions();
    for (const auto& failure : failures) {
        if (failure == "null") { continue; }
        INFO(failure);
        CHECK(functions[failure].get_type() == sol::type::function);
    }
}

/// The shutdown failure from data/json/lua/computer.lua breaks consoles next to the avatar.
/// When it finds one there, a console farther away on the same z-level stays intact.
TEST_CASE("computer_lua_shutdown_breaks_adjacent_console", "[computer][lua]") {
    clear_all_state();
    auto& here = get_map();
    const auto near = get_avatar().bub_pos() + point(1, 0);
    const auto far = get_avatar().bub_pos() + point(10, 0);
    here.ter_set(near, ter_str_id("t_console"));
    here.ter_set(far, ter_str_id("t_console"));
    auto comp = computer("Test", 0);
    auto session = computer_session(comp);

    session.activate_failure("shutdown");

    CHECK(here.ter(near).id() == ter_str_id("t_console_broken"));
    CHECK(here.ter(far).id() == ter_str_id("t_console"));
}

/// A mod registers failures in game.computer_failure_functions, like actions. This test runs
/// Lua source on the real state. The failure adds an option and a failure and raises security
/// through the session, and the computer save shows all three changes.
/// A failure with no Lua function reports a debug message.
TEST_CASE("computer_session_runs_lua_failure_that_edits_computer", "[computer][lua]") {
    clear_all_state();
    auto comp = computer("Test", 0);
    comp.add_failure("test_failure");
    auto session = computer_session(comp);
    lua_state().script(R"(
        game.computer_failure_functions.test_failure = function(params)
            params.session:add_option("Unlock doors", "unlock", 2)
            params.session:add_failure("alarm")
            params.session:set_security(params.session:get_security() + 3)
        end
    )");

    session.activate_failure("test_failure");
    computer_failure_functions()["test_failure"] = sol::lua_nil;

    CHECK(saved_actions(comp) == std::vector<std::string>{"unlock"});
    CHECK(saved_names(comp, "failures") == std::vector<std::string>{"test_failure", "alarm"});
    CHECK(saved_security(comp) == 3);

    const auto msg = capture_debugmsg_during([&] { session.activate_failure("no_such_failure"); });
    CHECK(msg.contains("no_such_failure"));
}

namespace {

struct event_log: public event_subscriber {
    auto notify(const cata::event& e) -> void override { events.push_back(e); }
    std::vector<cata::event> events;
};

} // namespace

/// The alarm failure sends triggers_alarm through gapi.send_event with a field table.
/// The event on the real bus must carry the avatar's id in its "character" field.
TEST_CASE("computer_lua_alarm_sends_event_with_avatar_id", "[computer][lua]") {
    clear_all_state();
    auto log = event_log{};
    g->events().subscribe(&log);
    auto comp = computer("Test", 0);
    auto session = computer_session(comp);

    session.activate_failure("alarm");
    g->events().unsubscribe(&log);

    REQUIRE(log.events.size() == 1);
    CHECK(log.events[0].type() == event_type::triggers_alarm);
    CHECK(log.events[0].get<character_id>("character") == get_avatar().getID());
}

/// The pump_leak failure floods 4 to 10 times, each time on a passable tile next to a pump.
/// A pump has a wall to the west and floor on the other 3 sides. The wall must stay, and at
/// least one floor tile must turn into sewage.
TEST_CASE("computer_lua_pump_leak_floods_passable_neighbours", "[computer][lua]") {
    clear_all_state();
    auto& here = get_map();
    const auto pump = get_avatar().bub_pos() + point(5, 5);
    here.ter_set(pump, ter_str_id("t_sewage_pump"));
    here.ter_set(pump + point_west, ter_str_id("t_concrete_wall"));
    const auto floors = std::vector{pump + point_north, pump + point_east, pump + point_south};
    for (const auto& p : floors) { here.ter_set(p, ter_str_id("t_floor")); }
    auto comp = computer("Test", 0);
    auto session = computer_session(comp);

    session.activate_failure("pump_leak");

    CHECK(here.ter(pump + point_west).id() == ter_str_id("t_concrete_wall"));
    CHECK(std::ranges::any_of(floors, [&](const auto& p) {
        return here.ter(p).id() == ter_str_id("t_sewage");
    }));
}

/// The manhacks failure spawns 4 to 8 manhacks with gapi.place_monster_within, each on a
/// free tile at most 3 tiles from the avatar.
TEST_CASE("computer_lua_manhacks_spawn_near_avatar", "[computer][lua]") {
    clear_all_state();
    auto comp = computer("Test", 0);
    auto session = computer_session(comp);

    session.activate_failure("manhacks");

    auto distances = std::vector<int>{};
    for (const auto& critter : g->all_monsters()) {
        if (critter.type->id == mtype_id("mon_manhack")) {
            distances.push_back(square_dist(critter.bub_pos(), get_avatar().bub_pos()));
        }
    }
    CHECK(distances.size() >= 4);
    CHECK(distances.size() <= 8);
    CHECK(std::ranges::all_of(distances, [](const auto d) { return d <= 3; }));
}

/// The terminate action kills monsters in glass cells: reinforced glass on one side and a
/// concrete wall on the other. A real zombie and a hallucinated zombie stand in two cells.
/// A Lua table stands in for the session because the real one waits for a key press.
/// The real zombie must die and the hallucination must stay.
TEST_CASE("computer_lua_terminate_skips_hallucinations", "[computer][lua]") {
    clear_all_state();
    auto& here = get_map();
    const auto real_pos = get_avatar().bub_pos() + point(3, 0);
    const auto fake_pos = get_avatar().bub_pos() + point(5, 0);
    for (const auto& p : {real_pos, fake_pos}) {
        here.ter_set(p + point_north, ter_str_id("t_reinforced_glass"));
        here.ter_set(p + point_south, ter_str_id("t_concrete_wall"));
    }
    auto& real = spawn_test_monster("mon_zombie", real_pos);
    auto& fake = spawn_test_monster("mon_zombie", fake_pos);
    fake.hallucination = true;

    auto& lua = lua_state();
    auto session = lua.create_table();
    session["query_any"] = [](const sol::table&, const std::string&) {};
    auto params = lua.create_table();
    params["session"] = session;
    params["user"] = &get_avatar();
    const auto res = computer_functions().get<sol::protected_function>("terminate")(params);
    REQUIRE(res.valid());

    CHECK(real.is_dead());
    CHECK_FALSE(fake.is_dead());
}

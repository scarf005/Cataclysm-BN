#include "../src/map/map.h"
#include "avatar.h"
#include "catalua_impl.h"
#include "catalua_sol.h"
#include "catch/catch.hpp"
#include "computer.h"
#include "computer_session.h"
#include "debug.h"
#include "fstream_utils.h"
#include "game.h"
#include "init.h"
#include "json.h"
#include "state_helpers.h"
#include "type_id.h"

#include <sstream>
#include <string>
#include <vector>

namespace {

auto lua_state() -> sol::state& { return DynamicDataLoader::get_instance().lua->lua; }

auto computer_functions() -> sol::table {
    return lua_state()["game"]["computer_functions"].get<sol::table>();
}

/// Reads the action names of a computer from its JSON save.
auto saved_actions(const computer& comp) -> std::vector<std::string> {
    auto is = std::istringstream(serialize(comp));
    auto jsin = JsonIn(is);
    const auto jo = jsin.get_object();
    jo.allow_omitted_members();
    auto actions = std::vector<std::string>{};
    for (const auto& opt : jo.get_array("options")) {
        const auto opt_jo = opt.get_object();
        opt_jo.allow_omitted_members();
        actions.push_back(opt_jo.get_string("action"));
    }
    return actions;
}

/// Legacy save string with one option per legacy action number 0..49.
auto legacy_save_with_every_action() -> std::string {
    auto out = std::ostringstream{};
    out << "Old_Terminal 0 -1 50";
    for (auto i = 0; i < 50; i++) { out << " opt " << i << " 0"; }
    out << " 0";
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
/// An action with no Lua function reports a debug message instead of doing nothing silently.
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

    const auto msg = capture_debugmsg_during([&] { session.activate_function("no_such_action"); });
    CHECK(msg.contains("no_such_action"));
}

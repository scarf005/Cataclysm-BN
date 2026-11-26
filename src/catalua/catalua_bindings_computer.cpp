#include "catalua_bindings.h"
#include "catalua_bindings_utils.h"
#include "catalua_luna.h"
#include "catalua_luna_doc.h"
#include "computer.h"
#include "computer_session.h"
#include "enum_conversions.h"
#include "mission.h"

auto cata::detail::reg_computer(sol::state& lua) -> void {
    DOC("An open computer terminal. `game.computer_functions` callbacks get it as `params.session`.");
    auto ut = luna::new_usertype<computer_session>(lua, luna::no_bases, luna::no_constructor);

    DOC("Clears the terminal.");
    luna::set_fx(ut, "reset_terminal", &computer_session::reset_terminal);
    luna::set_fx(ut, "print_newline", &computer_session::print_newline);
    DOC("Prints a line of random characters.");
    luna::set_fx(ut, "print_gibberish_line", &computer_session::print_gibberish_line);
    DOC("Prints the text as is. Format it with `string.format` first.");
    luna::set_fx(ut, "print_line", &computer_session::print_line);
    DOC("Like `print_line`, but in red.");
    luna::set_fx(ut, "print_error", &computer_session::print_error);
    DOC("Wraps and prints a block of text.");
    luna::set_fx(ut, "print_text", &computer_session::print_text);
    DOC("Prints the text and waits for yes, no or quit. Returns true for yes.");
    luna::set_fx(ut, "query_bool", &computer_session::query_bool);
    DOC("Prints the text, if given, and waits for any key.");
    luna::set_fx(ut, "query_any", [](computer_session& s, sol::optional<std::string> text) {
        text ? s.query_any(*text) : s.query_any();
    });
    DOC("Draws the terminal now, for output shown before a slow step.");
    luna::set_fx(ut, "redraw", &computer_session::redraw);

    DOC("Alert level of the computer. Each alert makes hacking harder.");
    luna::set_fx(ut, "get_alerts", &computer_session::get_alerts);
    luna::set_fx(ut, "set_alerts", &computer_session::set_alerts);
    DOC("The mission this computer belongs to, or nil.");
    luna::set_fx(ut, "get_mission", &computer_session::get_mission);
    DOC("Removes the first option that runs the given action.");
    luna::set_fx(ut, "remove_option", &computer_session::remove_option);
    DOC("Removes all options, which disables the terminal.");
    luna::set_fx(ut, "clear_options", &computer_session::clear_options);
    DOC("Runs a computer failure by id, for example `shutdown`.");
    luna::set_fx(ut, "activate_failure", [](computer_session& s, const std::string& id) {
        s.activate_failure(io::string_to_enum<computer_failure_type>(id));
    });
}

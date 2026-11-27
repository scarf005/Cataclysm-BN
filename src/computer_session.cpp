#include "computer_session.h"

#include "avatar.h"
#include "calendar.h"
#include "catalua.h"
#include "catalua_impl.h"
#include "catalua_sol.h"
#include "color.h"
#include "debug.h"
#include "enums.h"
#include "game.h"
#include "init.h"
#include "input.h"
#include "mission.h"
#include "options.h"
#include "output.h"
#include "point.h"
#include "rng.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"
#include "ui.h"
#include "ui_manager.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <utility>

static const skill_id skill_computer( "computer" );

namespace
{

/// Runs `game.<table>[key]` with the session and the avatar as `params`.
/// Returns false if the table has no such function.
auto run_lua_callback( computer_session &session, const std::string &table,
                       const std::string &key ) -> bool
{
    auto &state = *DynamicDataLoader::get_instance().lua;
    const auto func = cata::get_lua_callback( state, table, key );
    if( !func ) {
        debugmsg( "Computer callback '%s' is not in game.%s", key, table );
        return false;
    }
    try {
        auto params = state.lua.create_table();
        params["session"] = &session;
        params["user"] = &g->u;
        auto res = func( params );
        check_func_result( res );
    } catch( std::runtime_error &e ) {
        debugmsg( "Failed to run computer callback '%s' in game.%s: %s", key, table, e.what() );
    }
    return true;
}

} // namespace

static catacurses::window init_window()
{
    const int width = FULL_SCREEN_WIDTH;
    const int height = FULL_SCREEN_HEIGHT;
    const point p( std::max( 0, ( TERMX - width ) / 2 ), std::max( 0, ( TERMY - height ) / 2 ) );
    return catacurses::newwin( height, width, p );
}

computer_session::computer_session( computer &comp ) : comp( comp ),
    win( init_window() ), left( 1 ), top( 1 ), width( getmaxx( win ) - 2 ),
    height( getmaxy( win ) - 2 )
{
}

void computer_session::use()
{
    ui_adaptor ui;
    ui.on_screen_resize( [this]( ui_adaptor & ui ) {
        const int width = getmaxx( win );
        const int height = getmaxy( win );
        const point p( std::max( 0, ( TERMX - width ) / 2 ), std::max( 0, ( TERMY - height ) / 2 ) );
        win = catacurses::newwin( height, width, p );
        ui.position_from_window( win );
    } );
    ui.mark_resize();
    ui.on_redraw( [this]( const ui_adaptor & ) {
        refresh();
    } );

    // Login
    print_line( string_format( _( "Logging into %s…" ), comp.name ) );
    if( comp.security > 0 ) {
        if( calendar::turn < comp.next_attempt ) {
            print_error( _( "Access is temporarily blocked for security purposes." ) );
            query_any( _( "Please contact the system administrator." ) );
            reset_terminal();
            return;
        }
        print_error( comp.access_denied );
        switch( query_ynq( _( "Bypass security?" ) ) ) {
            case ynq::quit:
                reset_terminal();
                return;

            case ynq::no:
                query_any( _( "Shutting down… press any key." ) );
                reset_terminal();
                return;

            case ynq::yes:
                if( !hack_attempt( g->u ) ) {
                    if( comp.failures.empty() ) {
                        query_any( _( "Maximum login attempts exceeded.  Press any key…" ) );
                        reset_terminal();
                        return;
                    }
                    activate_random_failure();
                    reset_terminal();
                    return;
                } else { // Successful hack attempt
                    comp.security = 0;
                    query_any( _( "Login successful.  Press any key…" ) );
                    reset_terminal();
                }
        }
    } else { // No security
        query_any( _( "Login successful.  Press any key…" ) );
        reset_terminal();
    }

    // Main computer loop
    int sel = 0;
    while( true ) {
        size_t options_size = comp.options.size();

        uilist computer_menu;
        computer_menu.text = string_format( _( "%s - Root Menu" ), comp.name );
        computer_menu.selected = sel;
        computer_menu.fselected = sel;

        for( size_t i = 0; i < options_size; i++ ) {
            computer_menu.addentry( i, true, MENU_AUTOASSIGN, comp.options[i].name );
        }

        ui_manager::redraw();
        computer_menu.query();
        if( computer_menu.ret < 0 || static_cast<size_t>( computer_menu.ret ) >= comp.options.size() ) {
            break;
        }

        sel = computer_menu.ret;
        computer_option current = comp.options[sel];
        reset_terminal();
        // Once you trip the security, you have to roll every time you want to do something
        if( current.security + comp.alerts > 0 ) {
            print_error( _( "Password required." ) );
            if( query_bool( _( "Hack into system?" ) ) ) {
                if( !hack_attempt( g->u, current.security ) ) {
                    activate_random_failure();
                    reset_terminal();
                    return;
                } else {
                    // Successfully hacked function
                    comp.options[sel].security = 0;
                    activate_function( current.action );
                }
            }
        } else { // No need to hack, just activate
            activate_function( current.action );
        }
        reset_terminal();
        // Done processing a selected option.
    }

    reset_terminal(); // This should have been done by now, but just in case.
}

bool computer_session::hack_attempt( Character &who, int Security )
{
    if( Security == -1 ) {
        Security = comp.security;    // Set to main system security if no value passed
    }
    const int hack_skill = who.get_skill_level( skill_computer );

    // Every time you dig for lab notes, (or, in future, do other suspicious stuff?)
    // +2 dice to the system's hack-resistance
    // So practical max files from a given terminal = 5, at 10 Computer
    if( comp.alerts > 0 ) {
        Security += ( comp.alerts * 2 );
    }

    who.moves -= 10 * ( 5 + Security * 2 ) / std::max( 1, hack_skill + 1 );
    int player_roll = hack_skill;
    ///\EFFECT_INT <8 randomly penalizes hack attempts, 50% of the time
    if( who.int_cur < 8 && one_in( 2 ) ) {
        player_roll -= rng( 0, 8 - who.int_cur );
        ///\EFFECT_INT >8 randomly benefits hack attempts, 33% of the time
    } else if( who.int_cur > 8 && one_in( 3 ) ) {
        player_roll += rng( 0, who.int_cur - 8 );
    }

    ///\EFFECT_COMPUTER increases chance of successful hack attempt, vs Security level
    bool successful_attempt = ( dice( player_roll, 6 ) >= dice( Security, 6 ) );
    who.practice( skill_computer, successful_attempt ? ( 15 + Security * 3 ) : 7 );
    return successful_attempt;
}

void computer_session::activate_function( const std::string &action )
{
    if( action == "null" ) {
        return;
    }
    // Token move cost for any action, if an action takes more time than that it can be set in the action.
    if( run_lua_callback( *this, "computer_functions", action ) ) {
        g->u.moves -= 30;
    }
}

void computer_session::activate_random_failure()
{
    comp.next_attempt = calendar::turn + 45_minutes;
    static const auto default_failure = computer_failure( "shutdown" );
    activate_failure( random_entry( comp.failures, default_failure ).action );
}

void computer_session::activate_failure( const std::string &action )
{
    if( action != "null" ) {
        run_lua_callback( *this, "computer_failure_functions", action );
    }
}

auto computer_session::query_bool( const std::string &text ) -> bool
{
    return query_ynq( text ) == ynq::yes;
}

void computer_session::query_any( const std::string &text )
{
    print_line( text );
    query_any();
}

void computer_session::query_any()
{
    ui_manager::redraw();
    inp_mngr.wait_for_any_key();
}

void computer_session::redraw()
{
    ui_manager::redraw();
    refresh_display();
}

auto computer_session::get_alerts() const -> int
{
    return comp.alerts;
}

void computer_session::set_alerts( const int alerts )
{
    comp.alerts = alerts;
}

auto computer_session::get_mission() const -> mission * // *NOPAD*
{
    return mission::find( comp.mission_id );
}

void computer_session::add_option( const std::string &name, const std::string &action,
                                   const int security )
{
    comp.add_option( name, action, security );
}

void computer_session::remove_option( const std::string &action )
{
    comp.remove_option( action );
}

void computer_session::clear_options()
{
    comp.options.clear();
}

void computer_session::add_failure( const std::string &action )
{
    comp.add_failure( action );
}

auto computer_session::get_security() const -> int
{
    return comp.security;
}

void computer_session::set_security( const int security )
{
    comp.security = security;
}

auto computer_session::query_ynq( const std::string &text ) -> ynq
{
    const bool force_uc = get_option<bool>( "FORCE_CAPITAL_YN" );
    const auto &allow_key = force_uc ? input_context::disallow_lower_case
                            : input_context::allow_all_keys;
    input_context ctxt( "YESNOQUIT" );
    ctxt.register_action( "YES" );
    ctxt.register_action( "NO" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    print_line( string_format( force_uc
                               //~ 1st: query string, 2nd-4th: keybinding descriptions
                               ? pgettext( "query_ynq", "%s %s, %s, %s (Case sensitive)" )
                               //~ 1st: query string, 2nd-4th: keybinding descriptions
                               : pgettext( "query_ynq", "%s %s, %s, %s" ),
                               text,
                               ctxt.describe_key_and_name( "YES", allow_key ),
                               ctxt.describe_key_and_name( "NO", allow_key ),
                               ctxt.describe_key_and_name( "QUIT", allow_key ) ) );

    do {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( allow_key( ctxt.get_raw_input() ) ) {
            if( action == "YES" ) {
                return ynq::yes;
            } else if( action == "NO" ) {
                return ynq::no;
            } else if( action == "QUIT" ) {
                return ynq::quit;
            }
        }
    } while( true );
}

void computer_session::refresh()
{
    werase( win );
    draw_border( win );
    for( size_t i = 0; i < lines.size(); ++i ) {
        nc_color dummy = c_green;
        print_colored_text( win, point( left + lines[i].first, top + static_cast<int>( i ) ),
                            dummy, dummy, lines[i].second );
    }
    wnoutrefresh( win );
}

void computer_session::print_indented_line( const int indent, const int text_width,
        const std::string &text )
{
    if( text_width <= 0 || height <= 0 ) {
        return;
    }
    const size_t uheight = static_cast<size_t>( height );
    const auto folded = foldstring( text, text_width );
    if( folded.size() >= uheight ) {
        lines.clear();
    } else if( lines.size() + folded.size() > uheight ) {
        lines.erase( lines.begin(), lines.begin() + ( lines.size() + folded.size() - uheight ) );
    }
    lines.reserve( uheight );
    for( auto it = folded.size() >= uheight ? folded.end() - uheight : folded.begin();
         it < folded.end(); ++it ) {
        lines.emplace_back( indent, *it );
    }
}

void computer_session::print_line( const std::string &text )
{
    print_indented_line( 0, width, text );
}

void computer_session::print_error( const std::string &text )
{
    print_indented_line( 0, width, colorize( text, c_red ) );
}

void computer_session::print_text( const std::string &text )
{
    print_indented_line( 1, width - 2, text );
    print_newline();
}

void computer_session::print_gibberish_line()
{
    std::string gibberish;
    int length = rng( 50, 70 );
    for( int i = 0; i < length; i++ ) {
        switch( rng( 0, 4 ) ) {
            case 0:
                gibberish += static_cast<char>( '0' + rng( 0, 9 ) );
                break;
            case 1:
            case 2:
                gibberish += static_cast<char>( 'a' + rng( 0, 25 ) );
                break;
            case 3:
            case 4:
                gibberish += static_cast<char>( 'A' + rng( 0, 25 ) );
                break;
        }
    }
    print_indented_line( 0, width, colorize( gibberish, c_yellow ) );
}

void computer_session::reset_terminal()
{
    lines.clear();
}

void computer_session::print_newline()
{
    if( height <= 0 ) {
        return;
    }
    const size_t uheight = static_cast<size_t>( height );
    if( lines.size() >= uheight ) {
        lines.erase( lines.begin(), lines.end() - ( uheight - 1 ) );
    }
    lines.emplace_back();
}

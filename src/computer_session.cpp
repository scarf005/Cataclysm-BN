#include "computer_session.h"

#include "avatar.h"
#include "calendar.h"
#include "catalua.h"
#include "catalua_impl.h"
#include "catalua_sol.h"
#include "color.h"
#include "debug.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "explosion.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "init.h"
#include "input.h"
#include "int_id.h"
#include "item.h"
#include "item_contents.h"
#include "map/map.h"
#include "map/mapdata.h"
#include "map_iterator.h"
#include "messages.h"
#include "mission.h"
#include "monster.h"
#include "options.h"
#include "output.h"
#include "point.h"
#include "rng.h"
#include "sounds.h"
#include "string_formatter.h"
#include "timed_event.h"
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

static const efftype_id effect_amigara( "amigara" );

static const itype_id itype_usb_drive( "usb_drive" );
static const itype_id itype_vacutainer( "vacutainer" );

static const skill_id skill_computer( "computer" );

static const mtype_id mon_manhack( "mon_manhack" );
static const mtype_id mon_secubot( "mon_secubot" );

static const std::string flag_CONSOLE( "CONSOLE" );

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
    g->u.moves -= 30;
    auto &state = *DynamicDataLoader::get_instance().lua;
    const auto func = cata::get_lua_callback( state, "computer_functions", action );
    if( !func ) {
        debugmsg( "Computer action '%s' is not in game.computer_functions", action );
        return;
    }
    try {
        auto params = state.lua.create_table();
        params["session"] = this;
        params["user"] = &g->u;
        auto res = func( params );
        check_func_result( res );
    } catch( std::runtime_error &e ) {
        debugmsg( "Failed to run computer action '%s': %s", action, e.what() );
    }
}

void computer_session::activate_random_failure()
{
    comp.next_attempt = calendar::turn + 45_minutes;
    static const computer_failure default_failure( COMPFAIL_SHUTDOWN );
    const computer_failure &fail = random_entry( comp.failures, default_failure );
    activate_failure( fail.type );
}

const std::map<computer_failure_type, void( computer_session::* )()>
computer_session::computer_failure_functions = {
    { COMPFAIL_ALARM, &computer_session::failure_alarm },
    { COMPFAIL_AMIGARA, &computer_session::failure_amigara },
    { COMPFAIL_DAMAGE, &computer_session::failure_damage },
    { COMPFAIL_DESTROY_BLOOD, &computer_session::failure_destroy_blood },
    { COMPFAIL_DESTROY_DATA, &computer_session::failure_destroy_data },
    { COMPFAIL_MANHACKS, &computer_session::failure_manhacks },
    { COMPFAIL_PUMP_EXPLODE, &computer_session::failure_pump_explode },
    { COMPFAIL_PUMP_LEAK, &computer_session::failure_pump_leak },
    { COMPFAIL_SECUBOTS, &computer_session::failure_secubots },
    { COMPFAIL_SHUTDOWN, &computer_session::failure_shutdown },
};

void computer_session::activate_failure( computer_failure_type fail )
{
    const auto it = computer_failure_functions.find( fail );
    if( it != computer_failure_functions.end() ) {
        ( this->*( it->second ) )();
    }
}

void computer_session::failure_shutdown()
{
    bool found_tile = false;
    map &here = get_map();
    for( const auto &p : here.points_in_radius( g->u.bub_pos(), 1 ) ) {
        if( here.has_flag( flag_CONSOLE, p ) ) {
            here.ter_set( p, t_console_broken );
            add_msg( m_bad, _( "The console shuts down." ) );
            found_tile = true;
        }
    }
    if( found_tile ) {
        return;
    }
    for( const auto &p : here.points_on_zlevel() ) {
        if( here.has_flag( flag_CONSOLE, p ) ) {
            here.ter_set( p, t_console_broken );
            add_msg( m_bad, _( "The console shuts down." ) );
        }
    }
}

void computer_session::failure_alarm()
{
    g->events().send<event_type::triggers_alarm>( g->u.getID() );
    sound_event se;
    se.origin = g->u.bub_pos();
    se.volume = 100;
    se.category = sounds::sound_t::alarm;
    se.description = _( "an alarm sound!" );
    se.id = "environment";
    se.variant = "alarm";
    sounds::sound( se );
    if( g->get_levz() > 0 && !g->timed_events.queued( TIMED_EVENT_WANTED ) ) {
        g->timed_events.add( TIMED_EVENT_WANTED, calendar::turn + 30_minutes, 0,
                             g->u.abs_sm_pos() );
    }
}

void computer_session::failure_manhacks()
{
    int num_robots = rng( 4, 8 );
    const tripoint_range<tripoint_bub_ms> range =
        get_map().points_in_radius( get_player_character().bub_pos(), 3 );
    for( int i = 0; i < num_robots; i++ ) {
        if( g->place_critter_within( mon_manhack, range ) ) {
            add_msg( m_warning, _( "Manhacks drop from compartments in the ceiling." ) );
        }
    }
}

void computer_session::failure_secubots()
{
    int num_robots = 1;
    const tripoint_range<tripoint_bub_ms> range =
        get_map().points_in_radius( get_player_character().bub_pos(), 3 );
    for( int i = 0; i < num_robots; i++ ) {
        if( g->place_critter_within( mon_secubot, range ) ) {
            add_msg( m_warning, _( "Secubots emerge from compartments in the floor." ) );
        }
    }
}

void computer_session::failure_damage()
{
    add_msg( m_neutral, _( "The console shocks you." ) );
    if( g->u.is_elec_immune() ) {
        add_msg( m_good, _( "You're protected from electric shocks." ) );
    } else {
        add_msg( m_bad, _( "Your body is damaged by the electric shock!" ) );
        g->u.hurtall( rng( 1, 10 ), nullptr );
    }
}

void computer_session::failure_pump_explode()
{
    add_msg( m_warning, _( "The pump explodes!" ) );
    map &here = get_map();
    for( const auto &p : here.points_on_zlevel() ) {
        if( here.ter( p ) == t_sewage_pump ) {
            here.make_rubble( p );
            explosion_handler::explosion( p, nullptr, 10 );
        }
    }
}

void computer_session::failure_pump_leak()
{
    add_msg( m_warning, _( "Sewage leaks!" ) );
    map &here = get_map();
    for( const auto &p : here.points_on_zlevel() ) {
        if( here.ter( p ) != t_sewage_pump ) {
            continue;
        }
        const int leak_size = rng( 4, 10 );
        for( int i = 0; i < leak_size; i++ ) {
            std::vector<tripoint_bub_ms> next_move;
            if( here.passable( p + point_north ) ) {
                next_move.push_back( p + point_north );
            }
            if( here.passable( p + point_east ) ) {
                next_move.push_back( p + point_east );
            }
            if( here.passable( p + point_south ) ) {
                next_move.push_back( p + point_south );
            }
            if( here.passable( p + point_west ) ) {
                next_move.push_back( p + point_west );
            }
            if( next_move.empty() ) {
                break;
            }
            here.ter_set( random_entry( next_move ), t_sewage );
        }
    }
}

void computer_session::failure_amigara()
{
    g->timed_events.add( TIMED_EVENT_AMIGARA, calendar::turn + 30_seconds );
    g->u.add_effect( effect_amigara, 2_minutes );
    explosion_handler::explosion( tripoint_bub_ms( rng( 0, g_mapsize_x ), rng( 0, g_mapsize_y ),
                                  g->get_levz() ),
                                  nullptr,
                                  10,
                                  0.7, false, 10 );
    explosion_handler::explosion( tripoint_bub_ms( rng( 0, g_mapsize_x ), rng( 0, g_mapsize_y ),
                                  g->get_levz() ),
                                  nullptr,
                                  10,
                                  0.7, false, 10 );
    comp.remove_option( "amigara_start" );
}

void computer_session::failure_destroy_blood()
{
    print_error( _( "ERROR: Disruptive Spin" ) );
    map &here = get_map();
    for( const auto &dest : here.points_in_radius( g->u.bub_pos(), 2 ) ) {
        if( here.furn( dest ) == furn_str_id( "f_centrifuge" ) ) {
            map_stack items = here.i_at( dest );
            if( items.empty() ) {
                print_error( _( "ERROR: Please place sample in centrifuge." ) );
            } else if( items.size() > 1 ) {
                print_error( _( "ERROR: Please remove all but one sample from centrifuge." ) );
            } else if( items.only_item().typeId() != itype_vacutainer ) {
                print_error( _( "ERROR: Please use blood-contained samples." ) );
            } else if( items.only_item().contents.empty() ) {
                print_error( _( "ERROR: Blood draw kit, empty." ) );
            } else if( !items.only_item().contents.front().has_flag( flag_IS_BLOOD ) ) {
                print_error( _( "ERROR: Please only use blood samples." ) );
            } else {
                print_error( _( "ERROR: Blood sample destroyed." ) );
                here.i_clear( dest );
            }
        }
    }
    query_any();
}

void computer_session::failure_destroy_data()
{
    print_error( _( "ERROR: ACCESSING DATA MALFUNCTION" ) );
    map &here = get_map();
    for( const auto &p : here.points_in_radius( g->u.bub_pos(), 2 ) ) {
        if( here.ter( p ) == t_floor_blue ) {
            map_stack items = here.i_at( p );
            if( items.empty() ) {
                print_error( _( "ERROR: Please place memory bank in scan area." ) );
            } else if( items.size() > 1 ) {
                print_error( _( "ERROR: Please only scan one item at a time." ) );
            } else if( items.only_item().typeId() != itype_usb_drive ) {
                print_error( _( "ERROR: Memory bank destroyed or not present." ) );
            } else if( items.only_item().contents.empty() ) {
                print_error( _( "ERROR: Memory bank is empty." ) );
            } else {
                print_error( _( "ERROR: Data bank destroyed." ) );
                here.i_clear( p );
            }
        }
    }
    query_any();
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

void computer_session::remove_option( const std::string &action )
{
    comp.remove_option( action );
}

void computer_session::clear_options()
{
    comp.options.clear();
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

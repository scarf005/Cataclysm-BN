#include "computer.h"

#include <array>
#include <locale>
#include <sstream>

#include "json.h"
#include "string_utils.h"
#include "translations.h"

computer_option::computer_option()
    : name( "Unknown" ), action( "null" ), security( 0 )
{
}

computer_option::computer_option( const std::string &N, const std::string &A, int S )
    : name( N ), action( A ), security( S )
{
}

void computer_option::serialize( JsonOut &jout ) const
{
    jout.start_object();
    jout.member( "name", name );
    jout.member( "action", action );
    jout.member( "security", security );
    jout.end_object();
}

void computer_option::deserialize( JsonIn &jin )
{
    const JsonObject jo = jin.get_object();
    name = jo.get_string( "name" );
    action = jo.get_string( "action" );
    security = jo.get_int( "security" );
}

computer_failure::computer_failure()
    : action( "null" )
{
}

void computer_failure::serialize( JsonOut &jout ) const
{
    jout.start_object();
    jout.member( "action", action );
    jout.end_object();
}

void computer_failure::deserialize( JsonIn &jin )
{
    const JsonObject jo = jin.get_object();
    action = jo.get_string( "action" );
}

computer::computer( const std::string &new_name, int new_security )
    : name( new_name ), mission_id( -1 ), security( new_security ), alerts( 0 ),
      next_attempt( calendar::before_time_starts ),
      access_denied( _( "ERROR!  Access denied!" ) )
{
}

void computer::set_security( int Security )
{
    security = Security;
}

void computer::add_option( const computer_option &opt )
{
    options.emplace_back( opt );
}

void computer::add_option( const std::string &opt_name, const std::string &action,
                           int security )
{
    add_option( computer_option( opt_name, action, security ) );
}

void computer::add_failure( const computer_failure &failure )
{
    failures.emplace_back( failure );
}

void computer::add_failure( const std::string &action )
{
    add_failure( computer_failure( action ) );
}

void computer::set_access_denied_msg( const std::string &new_msg )
{
    access_denied = new_msg;
}

void computer::set_mission( const int id )
{
    mission_id = id;
}

namespace
{

auto computer_action_from_legacy_enum( const int val ) -> std::string
{
    // Used to migrate old saves. Do not reorder!
    static const auto names = std::array{
        "null",
        "open",
        "lock",
        "unlock",
        "toll",
        "sample",
        "release",
        "release_bionics",
        "terminate",
        "portal",
        "cascade",
        "research",
        "maps",
        "map_sewer",
        "map_subway",
        "null", // removed missile launch option, skipped in load_legacy_data()
        "miss_disarm",
        "list_bionics",
        "elevator_on",
        "amigara_log",
        "amigara_start",
        "complete_disable_external_power",
        "repeater_mod",
        "download_software",
        "blood_anal",
        "data_anal",
        "disconnect",
        "emerg_mess",
        "emerg_ref_center",
        "tower_unresponsive",
        "sr1_mess",
        "sr2_mess",
        "sr3_mess",
        "sr4_mess",
        "srcf_1_mess",
        "srcf_2_mess",
        "srcf_3_mess",
        "srcf_seal_order",
        "srcf_seal",
        "srcf_elevator",
        "open_disarm",
        "unlock_disarm",
        "release_disarm",
        "irradiator",
        "geiger",
        "conveyor",
        "shutters",
        "extract_rad_source",
        "deactivate_shock_vent",
        "radio_archive"
    };
    return val >= 0 && val < static_cast<int>( names.size() ) ? names[val] : "null";
}

auto computer_failure_from_legacy_enum( const int val ) -> std::string
{
    // Used to migrate old saves. Do not reorder!
    static const auto names = std::array{
        "null",
        "shutdown",
        "alarm",
        "manhacks",
        "secubots",
        "damage",
        "pump_explode",
        "pump_leak",
        "amigara",
        "destroy_blood",
        "destroy_data"
    };
    return val >= 0 && val < static_cast<int>( names.size() ) ? names[val] : "null";
}

} // namespace

void computer::load_legacy_data( const std::string &data )
{
    options.clear();
    failures.clear();

    std::istringstream dump( data );
    dump.imbue( std::locale::classic() );

    dump >> name >> security >> mission_id;

    name = replace_all( name, "_", " " );

    // Pull in options
    int optsize;
    dump >> optsize;
    for( int n = 0; n < optsize; n++ ) {
        std::string tmpname;

        int tmpaction;
        int tmpsec;

        dump >> tmpname >> tmpaction >> tmpsec;
        // Legacy missle launch option that got removed before `computer_action` was
        // refactored to be saved and loaded as string ids. Do not change this number:
        // `computer_action` now has different underlying values from back then!
        if( tmpaction == 15 ) {
            continue;
        }
        add_option( replace_all( tmpname, "_", " " ), computer_action_from_legacy_enum( tmpaction ),
                    tmpsec );
    }

    // Pull in failures
    int failsize;
    dump >> failsize;
    for( int n = 0; n < failsize; n++ ) {
        int tmpfail;
        dump >> tmpfail;
        add_failure( computer_failure_from_legacy_enum( tmpfail ) );
    }

    std::string tmp_access_denied;
    dump >> tmp_access_denied;

    // For backwards compatibility, only set the access denied message if it
    // isn't empty. This is to avoid the message becoming blank when people
    // load old saves.
    if( !tmp_access_denied.empty() ) {
        access_denied = replace_all( tmp_access_denied, "_", " " );
    }
}

void computer::serialize( JsonOut &jout ) const
{
    jout.start_object();
    jout.member( "name", name );
    jout.member( "mission", mission_id );
    jout.member( "security", security );
    jout.member( "alerts", alerts );
    jout.member( "next_attempt", next_attempt );
    jout.member( "options", options );
    jout.member( "failures", failures );
    jout.member( "access_denied", access_denied );
    jout.end_object();
}

void computer::deserialize( JsonIn &jin )
{
    if( jin.test_string() ) {
        load_legacy_data( jin.get_string() );
    } else {
        const JsonObject jo = jin.get_object();
        jo.read( "name", name );
        jo.read( "mission", mission_id );
        jo.read( "security", security );
        jo.read( "alerts", alerts );
        jo.read( "next_attempt", next_attempt );
        jo.read( "options", options );
        jo.read( "failures", failures );
        jo.read( "access_denied", access_denied );
    }
}

void computer::remove_option( const std::string &action )
{
    for( auto it = options.begin(); it != options.end(); ++it ) {
        if( it->action == action ) {
            options.erase( it );
            break;
        }
    }
}

computer_option computer_option::from_json( const JsonObject &jo )
{
    translation name;
    jo.read( "name", name );
    const auto action = jo.get_string( "action" );
    const int sec = jo.get_int( "security", 0 );
    return computer_option( name.translated(), action, sec );
}

computer_failure computer_failure::from_json( const JsonObject &jo )
{
    return computer_failure( jo.get_string( "action" ) );
}

#pragma once

#include <string>
#include <vector>

#include "calendar.h"

class JsonIn;
class JsonOut;
class JsonObject;

enum computer_failure_type {
    COMPFAIL_NULL = 0,
    COMPFAIL_ALARM,
    COMPFAIL_AMIGARA,
    COMPFAIL_DAMAGE,
    COMPFAIL_DESTROY_BLOOD,
    COMPFAIL_DESTROY_DATA,
    COMPFAIL_MANHACKS,
    COMPFAIL_PUMP_EXPLODE,
    COMPFAIL_PUMP_LEAK,
    COMPFAIL_SECUBOTS,
    COMPFAIL_SHUTDOWN,
    NUM_COMPUTER_FAILURES
};

template<typename E> struct enum_traits;

template<>
struct enum_traits<computer_failure_type> {
    static constexpr computer_failure_type last = NUM_COMPUTER_FAILURES;
};

struct computer_option {
    std::string name;
    /// Key of the Lua function in `game.computer_functions`.
    std::string action;
    int security;

    computer_option();
    computer_option( const std::string &N, const std::string &A, int S );
    // Save to/load from saves
    void serialize( JsonOut &jout ) const;
    void deserialize( JsonIn &jin );
    // Load from data files
    static computer_option from_json( const JsonObject &jo );
};

struct computer_failure {
    computer_failure_type type;

    computer_failure();
    computer_failure( computer_failure_type t ) : type( t ) {
    }
    // Save to/load from saves
    void serialize( JsonOut &jout ) const;
    void deserialize( JsonIn &jin );
    // Load from data files
    static computer_failure from_json( const JsonObject &jo );
};

class computer
{
    public:
        computer( const std::string &new_name, int new_security );

        // Initialization
        void set_security( int Security );
        void add_option( const computer_option &opt );
        void add_option( const std::string &opt_name, const std::string &action, int security );
        void add_failure( const computer_failure &failure );
        void add_failure( computer_failure_type failure );
        void set_access_denied_msg( const std::string &new_msg );
        void set_mission( int id );
        // Save/load
        void load_legacy_data( const std::string &data );
        void serialize( JsonOut &jout ) const;
        void deserialize( JsonIn &jin );

        friend class computer_session;
    private:
        // "Jon's Computer", "Lab 6E77-B Terminal Omega"
        std::string name;
        // Linked to a mission?
        int mission_id;
        // Difficulty of simply logging in
        int security;
        // Number of times security is tripped
        int alerts;
        // Date of next attempt
        time_point next_attempt;
        // Things we can do
        std::vector<computer_option> options;
        // Things that happen if we fail a hack
        std::vector<computer_failure> failures;
        // Message displayed when the computer is secured and initial login fails.
        // Can be customized to for example warn the player of potentially lethal
        // consequences like secubots spawning.
        std::string access_denied;

        void remove_option( const std::string &action );
};



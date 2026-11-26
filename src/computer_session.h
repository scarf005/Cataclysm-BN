#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "computer.h"
#include "cursesdef.h"

class Character;
class mission;

class computer_session
{
    public:
        computer_session( computer &comp );

        /** Handles player use of a computer */
        void use();

        /// Runs `game.computer_functions[action]` after the base 30 move cost.
        void activate_function( const std::string &action );
        void activate_failure( computer_failure_type fail );

        /// Terminal output for the Lua actions. Text is printed as is, without formatting.
        void reset_terminal();
        void print_newline();
        void print_gibberish_line();
        void print_line( const std::string &text );
        /// Like print_line, but in red.
        void print_error( const std::string &text );
        /// Wraps and prints a block of text with a 1-space indent.
        void print_text( const std::string &text );
        /// Prints the text and waits for Y/N/Q. Returns true for yes.
        auto query_bool( const std::string &text ) -> bool;
        /// Prints the text and waits for any key.
        void query_any( const std::string &text );
        /// Waits for any key without new output.
        void query_any();
        /// Draws the terminal now, for output shown before a slow step.
        void redraw();

        auto get_alerts() const -> int;
        void set_alerts( int alerts );
        /// The mission this computer belongs to, or nullptr.
        auto get_mission() const -> mission *; // *NOPAD*
        void remove_option( const std::string &action );
        void clear_options();

    private:
        computer &comp;
        // Output window. This class assumes win's size does not change.
        catacurses::window win;
        const int left;
        const int top;
        const int width;
        const int height;
        std::vector<std::pair<int, std::string>> lines;

        /** Returns true if the player successfully hacks the computer. Security = -1 defaults to
         *  the main system security. */
        bool hack_attempt( Character &who, int Security = -1 );

        // Generally called when we fail a hack attempt
        void activate_random_failure();

        // Actually displaying the terminal window
        void refresh();
        void print_indented_line( int indent, int text_width, const std::string &text );
        enum class ynq {
            yes,
            no,
            quit,
        };
        auto query_ynq( const std::string &text ) -> ynq;

        void failure_alarm();
        void failure_amigara();
        void failure_damage();
        void failure_destroy_blood();
        void failure_destroy_data();
        void failure_manhacks();
        void failure_pump_explode();
        void failure_pump_leak();
        void failure_secubots();
        void failure_shutdown();
        static const std::map<computer_failure_type, void( computer_session::* )()>
        computer_failure_functions;
};

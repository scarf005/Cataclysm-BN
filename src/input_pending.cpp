#include "input_pending.h"

#include "avatar.h"
#include "game.h"
#include "options.h"
#include "player_activity.h"

#if defined(TILES)
#    include "sdl_wrappers.h"
#endif

namespace
{

auto queued_input_redraw_scope_depth = 0;

auto game_allows_deferred_redraw() -> bool
{
    if( g == nullptr || g->uquit != QUIT_NO || g->safe_mode == SAFE_MODE_STOP ) {
        return false;
    }
    const auto &u = get_avatar();
    return !( u.activity && *u.activity );
}

} // namespace

auto key_down_event_pending() -> bool
{
#if defined(TILES)
    SDL_PumpEvents();
    return SDL_HasEvent( SDL_EVENT_KEY_DOWN );
#else
    return false;
#endif
}

auto should_defer_redraw_for_queued_input() -> bool
{
    return get_option<bool>( "DEFER_REDRAW_ON_QUEUED_INPUT" ) && game_allows_deferred_redraw() &&
           key_down_event_pending();
}

queued_input_redraw_scope::queued_input_redraw_scope() { ++queued_input_redraw_scope_depth; }

queued_input_redraw_scope::~queued_input_redraw_scope() { --queued_input_redraw_scope_depth; }

auto queued_input_redraw_scope::active() -> bool { return queued_input_redraw_scope_depth > 0; }

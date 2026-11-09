#pragma once

/// Whether a key-down event is already waiting in the platform input queue.
/// Always false on curses builds.
auto key_down_event_pending() -> bool;

/// Whether drawing the current frame can be skipped because the player already queued another key,
/// e.g. while holding a movement key.  The skipped invalidation persists, so the final frame is
/// drawn once the queue is empty.
///
/// Never skips when the `DEFER_REDRAW_ON_QUEUED_INPUT` option is off, when safe mode has just
/// stopped the player, while the death cam is up, or while the player runs an activity.
auto should_defer_redraw_for_queued_input() -> bool;

/// Marks the main game's action prompt as the current input reader, so the platform layer may
/// also skip presenting frames.  Popups and menus never set this.
class queued_input_redraw_scope
{
    public:
        queued_input_redraw_scope();
        ~queued_input_redraw_scope();
        queued_input_redraw_scope( const queued_input_redraw_scope & ) = delete;
        auto operator=( const queued_input_redraw_scope & ) -> queued_input_redraw_scope & = delete; // *NOPAD*

        /// Whether the main game's action prompt is currently reading input.
        static auto active() -> bool;
};

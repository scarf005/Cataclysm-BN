#include "creature_utils.h"
#include "creature.h"
#include "game.h"

template <typename T>
auto critter_by_id( const character_id &id ) -> T *
{
    return g->critter_by_id<T>( id );
}

/**
 * Returns the Creature at the given location. Optionally casted to the given
 * type of creature: @ref npc, @ref player, @ref monster - if there is a
 * creature, but it's not of the requested type, returns nullptr.
 * @param allow_hallucination Whether to return monsters that are actually
 * hallucinations.
 */
template <typename T>
auto critter_at_mut( const tripoint &p, bool allow_hallucination ) -> T *
{
    return g->critter_at<T>( p, allow_hallucination );
}

template <typename T>
auto critter_at( const tripoint &p, bool allow_hallucination ) -> const T *
{
    return g->critter_at<T>( p, allow_hallucination );
}

/**
 * Returns a shared pointer to the given critter (which can be of any of the
 * subclasses of
 * @ref Creature). The function may return an empty pointer if the given critter
 * is not stored anywhere (e.g. it was allocated on the stack, not stored in
 * the @ref critter_tracker nor in @ref active_npc nor is it @ref u).
 */
template <typename T>
auto shared_from( const T &critter ) -> shared_ptr_fast<T>
{
    return g->shared_from<T>( critter );
}

#pragma once
#ifndef CATA_SRC_CREATURE_UTILS_H
#define CATA_SRC_CREATURE_UTILS_H

#include "memory_fast.h"

struct tripoint;
class character_id;
class Creature;

/**
 * @return The living creature with the given id. Returns null if no living
 * creature with such an id exists. Never returns a dead creature.
 * Currently only the player character and npcs have ids.
 */
template <typename T = Creature>
auto critter_by_id( const character_id &id ) -> T *;

/**
 * Returns the Creature at the given location. Optionally casted to the given
 * type of creature: @ref npc, @ref player, @ref monster - if there is a
 * creature, but it's not of the requested type, returns nullptr.
 * @param allow_hallucination Whether to return monsters that are actually
 * hallucinations.
 */
template <typename T = Creature>
auto critter_at_mut( const tripoint &p, bool allow_hallucination = false ) -> T *;

template <typename T = Creature>
auto critter_at( const tripoint &p, bool allow_hallucination = false ) -> const T *;

/**
 * Returns a shared pointer to the given critter (which can be of any of the
 * subclasses of
 * @ref Creature). The function may return an empty pointer if the given critter
 * is not stored anywhere (e.g. it was allocated on the stack, not stored in
 * the @ref critter_tracker nor in @ref active_npc nor is it @ref u).
 */
template <typename T = Creature>
auto shared_from( const T &critter ) -> shared_ptr_fast<T>;

#endif // CATA_SRC_CREATURE_UTILS_Hs

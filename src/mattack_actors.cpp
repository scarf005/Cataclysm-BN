#include "mattack_actors.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>

#include "avatar.h"
#include "calendar.h"
#include "creature.h"
#include "enums.h"
#include "game.h"
#include "generic_factory.h"
#include "gun_mode.h"
#include "int_id.h"
#include "item.h"
#include "json.h"
#include "line.h"
#include "map.h"
#include "map_iterator.h"
#include "material.h"
#include "messages.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "player.h"
#include "point.h"
#include "ranged.h"
#include "rng.h"
#include "sounds.h"
#include "translations.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_range.h"

static const efftype_id effect_badpoison( "badpoison" );
static const efftype_id effect_bite( "bite" );
static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_infected( "infected" );
static const efftype_id effect_laserlocked( "laserlocked" );
static const efftype_id effect_poison( "poison" );
static const efftype_id effect_targeted( "targeted" );
static const efftype_id effect_was_laserlocked( "was_laserlocked" );

static const trait_id trait_TOXICFLESH( "TOXICFLESH" );
static const trait_id trait_NORANGEDCRIT( "NO_RANGED_CRIT" );

// Simplified version of the function in monattack.cpp
static bool is_adjacent( const monster &z, const Creature &target )
{
    if( rl_dist( z.pos(), target.pos() ) != 1 ) {
        return false;
    }

    if( !z.can_squeeze_to( target.pos() ) ) {
        return false;
    }

    return z.posz() == target.posz();
}

void leap_actor::load_internal( const JsonObject &obj, const std::string & )
{
    // Mandatory:
    max_range = obj.get_float( "max_range" );
    // Optional:
    min_range = obj.get_float( "min_range", 1.0f );
    allow_no_target = obj.get_bool( "allow_no_target", false );
    move_cost = obj.get_int( "move_cost", 150 );
    min_consider_range = obj.get_float( "min_consider_range", 0.0f );
    max_consider_range = obj.get_float( "max_consider_range", 200.0f );
}

std::unique_ptr<mattack_actor> leap_actor::clone() const
{
    return std::make_unique<leap_actor>( *this );
}

bool leap_actor::call( monster &z ) const
{
    if( !z.can_act() || !z.move_effects( false ) ) {
        return false;
    }

    std::vector<tripoint> options;
    tripoint target = z.move_target();
    float best_float = trigdist ? trig_dist( z.pos(), target ) : square_dist( z.pos(), target );
    if( best_float < min_consider_range || best_float > max_consider_range ) {
        return false;
    }

    // We wanted the float for range check
    // int here will make the jumps more random
    int best = std::numeric_limits<int>::max();
    if( !allow_no_target && z.attack_target() == nullptr ) {
        return false;
    }
    map &here = get_map();
    std::multimap<int, tripoint> candidates;
    for( const tripoint &candidate : here.points_in_radius( z.pos(), max_range ) ) {
        if( candidate == z.pos() ) {
            continue;
        }
        float leap_dist = trigdist ? trig_dist( z.pos(), candidate ) :
                          square_dist( z.pos(), candidate );
        if( leap_dist > max_range || leap_dist < min_range ) {
            continue;
        }
        int candidate_dist = rl_dist( candidate, target );
        if( candidate_dist >= best_float ) {
            continue;
        }
        candidates.emplace( candidate_dist, candidate );
    }
    for( const auto &candidate : candidates ) {
        const int &cur_dist = candidate.first;
        const tripoint &dest = candidate.second;
        if( cur_dist > best ) {
            break;
        }
        if( !z.sees( dest ) ) {
            continue;
        }
        if( !g->is_empty( dest ) ) {
            continue;
        }
        bool blocked_path = false;
        // check if monster has a clear path to the proposed point
        std::vector<tripoint> line = here.find_clear_path( z.pos(), dest );
        tripoint prev_point = z.pos();
        for( auto &i : line ) {
            if( here.impassable( i ) || here.obstructed_by_vehicle_rotation( prev_point, i ) ) {
                blocked_path = true;
                break;
            }
            prev_point = i;
        }
        // don't leap into water if you could drown (#38038)
        if( z.is_aquatic_danger( dest ) ) {
            blocked_path = true;
        }
        if( blocked_path ) {
            continue;
        }

        options.push_back( dest );
        best = cur_dist;
    }

    if( options.empty() ) {
        return false;    // Nowhere to leap!
    }

    z.moves -= move_cost;
    const tripoint chosen = random_entry( options );
    bool seen = g->u.sees( z ); // We can see them jump...
    z.setpos( chosen );
    seen |= g->u.sees( z ); // ... or we can see them land
    if( seen ) {
        add_msg( _( "The %s leaps!" ), z.name() );
    }

    return true;
}

std::unique_ptr<mattack_actor> mon_spellcasting_actor::clone() const
{
    return std::make_unique<mon_spellcasting_actor>( *this );
}

void mon_spellcasting_actor::load_internal( const JsonObject &obj, const std::string & )
{
    std::string sp_id;
    fake_spell intermediate;
    mandatory( obj, was_loaded, "spell_data", intermediate );
    self = intermediate.self;
    translation monster_message;
    optional( obj, was_loaded, "monster_message", monster_message,
              //~ "<Monster Display name> cast <Spell Name> on <Target name>!"
              to_translation( "%1$s casts %2$s at %3$s!" ) );
    spell_data = intermediate.get_spell();
    spell_data.set_message( monster_message );
}

void mon_spellcasting_actor::finalize()
{
    avatar fake_player;
    move_cost = spell_data.casting_time( fake_player );
}

bool mon_spellcasting_actor::call( monster &mon ) const
{
    if( !mon.can_act() ) {
        return false;
    }

    if( !mon.attack_target() ) {
        // this is an attack. there is no reason to attack if there isn't a real target.
        return false;
    }

    const tripoint target = self ? mon.pos() : mon.attack_target()->pos();

    std::string fx = spell_data.effect();
    // is the spell an attack that needs to hit the target?
    // examples of spells that don't: summons, teleport self
    const bool targeted_attack = fx == "target_attack" || fx == "projectile_attack" ||
                                 fx == "cone_attack" || fx == "line_attack";

    if( targeted_attack && rl_dist( mon.pos(), target ) > spell_data.range() ) {
        return false;
    }

    std::string target_name;
    if( const Creature *target_monster = g->critter_at( target ) ) {
        target_name = target_monster->disp_name();
    }

    if( g->u.sees( target ) ) {
        add_msg( spell_data.message(), mon.disp_name(), spell_data.name(), target_name );
    }

    spell_data.cast_all_effects( mon, target );

    return true;
}

melee_actor::melee_actor()
{
    damage_max_instance = damage_instance::physical( 9, 0, 0, 0 );
    min_mul = 0.5f;
    max_mul = 1.0f;
    move_cost = 100;
}

melee_actor::~melee_actor() = default;

void melee_actor::load_internal( const JsonObject &obj, const std::string & )
{
    // Optional:
    if( obj.has_array( "damage_max_instance" ) ) {
        damage_max_instance = load_damage_instance( obj.get_array( "damage_max_instance" ) );
    } else if( obj.has_object( "damage_max_instance" ) ) {
        damage_max_instance = load_damage_instance( obj );
    }

    min_mul = obj.get_float( "min_mul", 0.0f );
    max_mul = obj.get_float( "max_mul", 1.0f );
    move_cost = obj.get_int( "move_cost", 100 );
    accuracy = obj.get_int( "accuracy", INT_MIN );

    optional( obj, was_loaded, "miss_msg_u", miss_msg_u,
              to_translation( "The %s lunges at you, but you dodge!" ) );
    optional( obj, was_loaded, "no_dmg_msg_u", no_dmg_msg_u,
              to_translation( "The %1$s bites your %2$s, but fails to penetrate armor!" ) );
    optional( obj, was_loaded, "hit_dmg_u", hit_dmg_u,
              to_translation( "The %1$s bites your %2$s!" ) );
    optional( obj, was_loaded, "miss_msg_npc", miss_msg_npc,
              to_translation( "The %s lunges at <npcname>, but they dodge!" ) );
    optional( obj, was_loaded, "no_dmg_msg_npc", no_dmg_msg_npc,
              to_translation( "The %1$s bites <npcname>'s %2$s, but fails to penetrate armor!" ) );
    optional( obj, was_loaded, "hit_dmg_npc", hit_dmg_npc,
              to_translation( "The %1$s bites <npcname>'s %2$s!" ) );

    if( obj.has_array( "body_parts" ) ) {
        for( JsonArray sub : obj.get_array( "body_parts" ) ) {
            const body_part bp = get_body_part_token( sub.get_string( 0 ) );
            const float prob = sub.get_float( 1 );
            body_parts.add_or_replace( bp, prob );
        }
    }

    if( obj.has_array( "effects" ) ) {
        for( JsonObject eff : obj.get_array( "effects" ) ) {
            effects.push_back( load_mon_effect_data( eff ) );
        }
    }
}

Creature *melee_actor::find_target( monster &z ) const
{
    if( !z.can_act() ) {
        return nullptr;
    }

    Creature *target = z.attack_target();
    if( target == nullptr || !is_adjacent( z, *target ) ) {
        return nullptr;
    }

    return target;
}

bool melee_actor::call( monster &z ) const
{
    Creature *target = find_target( z );
    if( target == nullptr ) {
        return false;
    }

    z.mod_moves( -move_cost );

    add_msg( m_debug, "%s attempting to melee_attack %s", z.name(),
             target->disp_name() );

    const int acc = accuracy >= 0 ? accuracy : z.type->melee_skill;
    int hitspread = target->deal_melee_attack( &z, dice( acc, 10 ) );

    if( hitspread < 0 ) {
        auto msg_type = target->is_avatar() ? m_warning : m_info;
        sfx::play_variant_sound( "mon_bite", "bite_miss", sfx::get_heard_volume( z.pos() ),
                                 sfx::get_heard_angle( z.pos() ) );
        target->add_msg_player_or_npc( msg_type, miss_msg_u, miss_msg_npc, z.name() );
        return true;
    }

    damage_instance damage = damage_max_instance;
    damage.add_damage( DT_BASH, z.get_bash_bonus() );
    damage.add_damage( DT_CUT, z.get_cut_bonus() );

    double multiplier = rng_float( min_mul, max_mul );
    damage.mult_damage( multiplier );

    const bodypart_str_id &bp_hit = body_parts.empty() ?
                                    target->select_body_part( &z, hitspread ) :
                                    convert_bp( *body_parts.pick() );

    target->on_hit( &z, bp_hit.id() );
    dealt_damage_instance dealt_damage = target->deal_damage( &z, bp_hit.id(), damage );
    dealt_damage.bp_hit = bp_hit;

    int damage_total = dealt_damage.total_damage();
    add_msg( m_debug, "%s's melee_attack did %d damage", z.name(), damage_total );
    if( damage_total > 0 ) {
        on_damage( z, *target, dealt_damage );
    } else {
        sfx::play_variant_sound( "mon_bite", "bite_miss", sfx::get_heard_volume( z.pos() ),
                                 sfx::get_heard_angle( z.pos() ) );
        target->add_msg_player_or_npc( m_neutral, no_dmg_msg_u, no_dmg_msg_npc, z.name(),
                                       body_part_name_accusative( bp_hit ) );
    }

    return true;
}

// TODO: Remove after effect permanence change
#include "effect.h"
void melee_actor::on_damage( monster &z, Creature &target, dealt_damage_instance &dealt ) const
{
    if( target.is_player() ) {
        sfx::play_variant_sound( "mon_bite", "bite_hit", sfx::get_heard_volume( z.pos() ),
                                 sfx::get_heard_angle( z.pos() ) );
        sfx::do_player_death_hurt( dynamic_cast<player &>( target ), false );
    }
    auto msg_type = target.attitude_to( g->u ) == Attitude::A_FRIENDLY ? m_bad : m_neutral;
    const bodypart_str_id bp = dealt.bp_hit;
    target.add_msg_player_or_npc( msg_type, hit_dmg_u, hit_dmg_npc, z.name(),
                                  body_part_name_accusative( bp ) );

    for( const auto &eff : effects ) {
        if( x_in_y( eff.chance, 100 ) ) {
            const bodypart_str_id &affected_bp = eff.affect_hit_bp ? bp : convert_bp( eff.bp );
            target.add_effect( eff.id, time_duration::from_turns( eff.duration ), affected_bp );
            if( eff.permanent ) {
                target.get_effect( eff.id, affected_bp ).set_permanent();
            }
        }
    }
}

std::unique_ptr<mattack_actor> melee_actor::clone() const
{
    return std::make_unique<melee_actor>( *this );
}

bite_actor::bite_actor() = default;

void bite_actor::load_internal( const JsonObject &obj, const std::string &src )
{
    melee_actor::load_internal( obj, src );
    no_infection_chance = obj.get_int( "no_infection_chance", 14 );
}

void bite_actor::on_damage( monster &z, Creature &target, dealt_damage_instance &dealt ) const
{
    melee_actor::on_damage( z, target, dealt );
    if( target.has_effect( effect_grabbed ) && one_in( no_infection_chance - dealt.total_damage() ) ) {
        const bodypart_str_id &hit = dealt.bp_hit;
        if( target.has_effect( effect_bite, hit ) ) {
            target.add_effect( effect_bite, 40_minutes, hit );
        } else if( target.has_effect( effect_infected, hit ) ) {
            target.add_effect( effect_infected, 25_minutes, hit );
        } else {
            target.add_effect( effect_bite, 1_turns, hit );
        }
    }
    if( target.has_trait( trait_TOXICFLESH ) ) {
        z.add_effect( effect_poison, 5_minutes );
        z.add_effect( effect_badpoison, 5_minutes );
    }
}

std::unique_ptr<mattack_actor> bite_actor::clone() const
{
    return std::make_unique<bite_actor>( *this );
}

gun_actor::gun_actor() : description( _( "The %1$s fires its %2$s!" ) ),
    targeting_sound( _( "beep-beep-beep!" ) )
{
}

void gun_actor::load_internal( const JsonObject &obj, const std::string & )
{
    obj.read( "gun_type", gun_type, true );

    obj.read( "ammo_type", ammo_type );

    if( obj.has_array( "fake_skills" ) ) {
        for( JsonArray cur : obj.get_array( "fake_skills" ) ) {
            fake_skills[skill_id( cur.get_string( 0 ) )] = cur.get_int( 1 );
        }
    }

    obj.read( "fake_str", fake_str );
    obj.read( "fake_dex", fake_dex );
    obj.read( "fake_int", fake_int );
    obj.read( "fake_per", fake_per );

    for( JsonArray mode : obj.get_array( "ranges" ) ) {
        if( mode.size() < 2 || mode.get_int( 0 ) > mode.get_int( 1 ) ) {
            obj.throw_error( "incomplete or invalid range specified", "ranges" );
        }
        ranges.emplace( std::make_pair<int, int>( mode.get_int( 0 ), mode.get_int( 1 ) ),
                        gun_mode_id( mode.size() > 2 ? mode.get_string( 2 ) : "" ) );
    }

    obj.read( "max_ammo", max_ammo );

    obj.read( "move_cost", move_cost );

    if( obj.read( "description", description ) ) {
        description = _( description );
    }
    if( obj.read( "failure_msg", failure_msg ) ) {
        failure_msg = _( failure_msg );
    }
    if( obj.read( "no_ammo_sound", no_ammo_sound ) ) {
        no_ammo_sound = _( no_ammo_sound );
    } else {
        no_ammo_sound = _( "Click." );
    }

    obj.read( "targeting_cost", targeting_cost );

    obj.read( "require_targeting_player", require_targeting_player );
    obj.read( "require_targeting_npc", require_targeting_npc );
    obj.read( "require_targeting_monster", require_targeting_monster );

    obj.read( "targeting_timeout", targeting_timeout );
    obj.read( "targeting_timeout_extend", targeting_timeout_extend );

    if( obj.read( "targeting_sound", targeting_sound ) ) {
        targeting_sound = _( targeting_sound );
    } else {
        targeting_sound = _( "Beep." );
    }

    obj.read( "targeting_volume", targeting_volume );

    obj.read( "laser_lock", laser_lock );

    obj.read( "target_moving_vehicles", target_moving_vehicles );

    obj.read( "require_sunlight", require_sunlight );

    obj.read( "no_crits", no_crits );
}

std::unique_ptr<mattack_actor> gun_actor::clone() const
{
    return std::make_unique<gun_actor>( *this );
}

int gun_actor::get_max_range()  const
{
    int max_range = 0;
    for( const auto &e : ranges ) {
        max_range = std::max( std::max( max_range, e.first.first ), e.first.second );
    }
    return max_range;
}
namespace
{

auto find_target_vehicle( monster &z, int range ) -> std::optional<tripoint>
{
    const auto is_different_plane = []( const wrapped_vehicle & v, const monster & m ) -> bool {
        return !fov_3d && v.pos.z != m.pos().z;
    };

    map &here = get_map();
    bool found = false;
    tripoint aim_at;
    for( wrapped_vehicle &v : here.get_vehicles() ) {
        if( is_different_plane( v, z ) || v.v->velocity == 0 ) {
            continue;
        }

        bool found_controls = false;

        for( const vpart_reference &vp : v.v->get_avail_parts( "CONTROLS" ) ) {
            if( !z.sees( vp.pos() ) ) {
                continue;
            }

            int new_dist = rl_dist( z.pos(), vp.pos() );
            if( new_dist <= range ) {
                aim_at = vp.pos();
                range = new_dist;
                found = true;
                found_controls = true;
            }
        }


        if( !found_controls ) {
            std::vector<tripoint> line = here.find_clear_path( z.pos(), v.v->global_pos3() );
            tripoint prev_point = z.pos();
            for( tripoint &i : line ) {
                if( !z.sees( i ) ||  here.floor_between( prev_point, i ) ) {
                    break;
                }
                optional_vpart_position vp = here.veh_at( i );
                if( vp && &vp->vehicle() == v.v ) {
                    int new_dist = rl_dist( z.pos(), i );
                    if( new_dist <= range ) {
                        aim_at = i;
                        range = new_dist;
                        found = true;
                    }
                    break;
                }
                if( !here.is_transparent( i ) ) {
                    break;
                }
                prev_point = i;
            }
        }
    }
    if( found ) {
        return std::make_optional( aim_at );
    } else {
        return std::optional<tripoint>();
    }
}

} // namespace


bool gun_actor::call( monster &z ) const
{
    Creature *target;
    tripoint aim_at;
    bool untargeted = false;

    if( z.friendly ) {
        int max_range = get_max_range();

        int hostiles; // hostiles which cannot be engaged without risking friendly fire
        target = z.auto_find_hostile_target( max_range, hostiles );
        if( !target ) {
            if( hostiles > 0 && g->u.sees( z ) ) {
                add_msg( m_warning, vgettext( "Pointed in your direction, the %s emits an IFF warning beep.",
                                              "Pointed in your direction, the %s emits %d annoyed sounding beeps.",
                                              hostiles ),
                         z.name(), hostiles );
            }
            return false;
        }
        aim_at = target->pos();
    } else {
        target = z.attack_target();
        if( !target || ( !target->is_monster() && !z.aggro_character ) || !z.sees( *target ) ) {
            if( !target_moving_vehicles ) {
                return false;
            }
            //No living targets, try to find a moving car
            std::optional<tripoint> aim = find_target_vehicle( z, get_max_range() );
            if( !aim ) {
                return false;
            }
            aim_at = *aim;
            untargeted = true;
        } else {
            aim_at = target->pos();
        }
    }

    // One last check to make sure we're not firing on a friendly
    if( target && z.attitude_to( *target ) == Attitude::A_FRIENDLY ) {
        return false;
    }
    int dist = rl_dist( z.pos(), aim_at );
    for( const auto &e : ranges ) {
        if( dist >= e.first.first && dist <= e.first.second ) {
            if( untargeted || try_target( z, *target ) ) {
                shoot( z, aim_at, e.second );
            }

            return true;
        }
    }
    return false;
}

bool gun_actor::try_target( monster &z, Creature &target ) const
{
    if( require_sunlight && !g->is_in_sunlight( z.pos() ) ) {
        if( one_in( 3 ) && g->u.sees( z ) ) {
            add_msg( _( failure_msg ), z.name() );
        }
        return false;
    }

    const bool require_targeting = ( require_targeting_player && target.is_player() ) ||
                                   ( require_targeting_npc && target.is_npc() ) ||
                                   ( require_targeting_monster && target.is_monster() );
    const bool not_targeted = require_targeting && !z.has_effect( effect_targeted );
    const bool not_laser_locked = require_targeting && laser_lock &&
                                  !target.has_effect( effect_was_laserlocked );

    if( not_targeted || not_laser_locked ) {
        if( targeting_volume > 0 && !targeting_sound.empty() ) {
            sounds::sound( z.pos(), targeting_volume, sounds::sound_t::alarm,
                           _( targeting_sound ) );
        }
        if( not_targeted ) {
            z.add_effect( effect_targeted, time_duration::from_turns( targeting_timeout ) );
        }
        if( not_laser_locked ) {
            target.add_effect( effect_laserlocked, 5_turns );
            target.add_effect( effect_was_laserlocked, 5_turns );
            target.add_msg_if_player( m_warning,
                                      _( "You're not sure why you've got a laser dot on you…" ) );
        }

        z.moves -= targeting_cost;
        return false;
    }

    if( require_targeting ) {
        z.add_effect( effect_targeted, time_duration::from_turns( targeting_timeout_extend ) );
    }

    if( laser_lock ) {
        // To prevent spamming laser locks when the player can tank that stuff somehow
        target.add_effect( effect_was_laserlocked, 5_turns );
    }
    return true;
}

void gun_actor::shoot( monster &z, const tripoint &target, const gun_mode_id &mode,
                       int inital_recoil ) const
{
    z.moves -= move_cost;

    detached_ptr<item> gun = item::spawn( gun_type );
    gun->gun_set_mode( mode );

    itype_id ammo = ammo_type ? ammo_type : gun->ammo_default();
    if( ammo ) {
        gun->ammo_set( ammo, z.ammo[ammo] );
    }

    if( !gun->ammo_sufficient() ) {
        if( !no_ammo_sound.empty() ) {
            sounds::sound( z.pos(), 10, sounds::sound_t::combat, _( no_ammo_sound ) );
        }
        return;
    }

    standard_npc tmp( _( "The " ) + z.name(), z.pos(), {}, 8,
                      fake_str, fake_dex, fake_int, fake_per );
    tmp.set_fake( true );
    tmp.set_attitude( z.friendly ? NPCATT_FOLLOW : NPCATT_KILL );
    tmp.recoil = inital_recoil; // set inital recoil
    if( no_crits ) {
        tmp.toggle_trait( trait_NORANGEDCRIT );
    }

    for( const auto &pr : fake_skills ) {
        tmp.set_skill_level( pr.first, pr.second );
    }
    int qty = gun->gun_current_mode().qty;
    tmp.set_primary_weapon( std::move( gun ) );
    tmp.i_add( item::spawn( "UPS_off", calendar::turn, 1000 ) );

    if( g->u.sees( z ) ) {
        add_msg( m_warning, _( description ), z.name(), tmp.primary_weapon().tname() );
    }

    z.ammo[ammo] -= ranged::fire_gun( tmp, target, qty );
}

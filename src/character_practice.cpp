#include "character.h"
#include "character_functions.h"
#include "event.h"
#include "event_bus.h"
#include "game.h"
#include "messages.h"
#include "rng.h"
#include "skill.h"

static const trait_id trait_SAVANT( "SAVANT" );
static const trait_id trait_PACIFIST( "PACIFIST" );

void Character::practice( const skill_id &id, int amount, int cap, bool suppress_warning )
{
    SkillLevel &level = get_skill_level_object( id );
    const Skill &skill = id.obj();
    std::string skill_name = skill.name();

    if( !level.can_train() && !in_sleep_state() ) {
        // If leveling is disabled, don't train, don't drain focus, don't print anything
        // Leaving as a skill method rather than global for possible future skill cap setting
        return;
    }

    const auto highest_skill = [&]() {
        std::pair<skill_id, int> result( skill_id::NULL_ID(), -1 );
        for( const auto &pair : *_skills ) {
            const SkillLevel &lobj = pair.second;
            if( lobj.level() > result.second ) {
                result = std::make_pair( pair.first, lobj.level() );
            }
        }
        return result.first;
    };

    const bool isSavant = has_trait( trait_SAVANT );
    const skill_id savantSkill = isSavant ? highest_skill() : skill_id::NULL_ID();

    amount = adjust_for_focus( amount );

    if( has_trait( trait_PACIFIST ) && skill.is_combat_skill() ) {
        if( !one_in( 3 ) ) {
            amount = 0;
        }
    }
    if( has_trait_flag( trait_flag_str_id( "PRED2" ) ) && skill.is_combat_skill() ) {
        if( one_in( 3 ) ) {
            amount *= 2;
        }
    }
    if( has_trait_flag( trait_flag_str_id( "PRED3" ) ) && skill.is_combat_skill() ) {
        amount *= 2;
    }

    if( has_trait_flag( trait_flag_str_id( "PRED4" ) ) && skill.is_combat_skill() ) {
        amount *= 3;
    }

    if( isSavant && id != savantSkill ) {
        amount /= 2;
    }

    if( amount > 0 && get_skill_level( id ) > cap ) { //blunt grinding cap implementation for crafting
        amount = 0;
        if( !suppress_warning && one_in( 5 ) ) {
            character_funcs::show_skill_capped_notice( *this, id );
        }
    }
    if( amount > 0 && level.isTraining() ) {
        int oldLevel = get_skill_level( id );
        get_skill_level_object( id ).train( amount );
        int newLevel = get_skill_level( id );
        if( newLevel > oldLevel ) {
            g->events().send<event_type::gains_skill_level>( getID(), id, newLevel );
        }
        if( is_player() && newLevel > oldLevel ) {
            add_msg( m_good, _( "Your skill in %s has increased to %d!" ), skill_name, newLevel );
        }
        if( is_player() && newLevel > cap ) {
            //inform player immediately that the current recipe can't be used to train further
            add_msg( m_info, _( "You feel that %s tasks of this level are becoming trivial." ),
                     skill_name );
        }

        int chance_to_drop = focus_pool;
        focus_pool -= chance_to_drop / 100;
        // Apex Predators don't think about much other than killing.
        // They don't lose Focus when practicing combat skills.
        if( ( rng( 1, 100 ) <= ( chance_to_drop % 100 ) ) &&
            ( !( has_trait_flag( trait_flag_str_id( "PRED4" ) ) &&
                 skill.is_combat_skill() ) ) ) {
            focus_pool--;
        }
    }

    get_skill_level_object( id ).practice();
}

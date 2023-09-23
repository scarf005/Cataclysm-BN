#include <algorithm>
#include <numeric>

#include "assign.h"
#include "crafting.h"
#include "game_inventory.h"
#include "item.h"
#include "iuse_actor.h"
#include "json.h"
#include "recipe_dictionary.h"
#include "map.h"
#include "material.h"
#include "messages.h"
#include "output.h"
#include "player.h"
#include "rng.h"
#include "type_id.h"

static const skill_id skill_fabrication( "fabrication" );

namespace
{

auto minimal_weight_to_cut( const std::vector<material_id> &xs ) -> units::mass
{
    return std::accumulate( xs.cbegin(), xs.cend(), units::mass_max,
    []( units::mass acc, auto &&mat_id ) -> units::mass {
        const std::optional<itype_id> id = mat_id->salvaged_into();

        return id ? std::min( acc, item( *id ).weight() ) : acc;
    } );
}

auto total_material_weight( const std::vector<material_id> &xs ) -> units::mass
{
    return std::accumulate( xs.cbegin(), xs.cend(), 0_gram,
    []( units::mass acc, auto &&mat_id ) -> units::mass {
        const std::optional<itype_id> id = mat_id->salvaged_into();

        return id ? acc + item( *id ).weight() : 0_gram;
    } );
}

} // namespace

auto salvage_actor::load( const JsonObject &obj ) -> void
{
    assign( obj, "cost", cost );
    assign( obj, "moves_per_part", moves_per_part );

    if( obj.has_array( "material_whitelist" ) ) {
        material_whitelist.clear();
        assign( obj, "material_whitelist", material_whitelist );
    }
}

auto salvage_actor::clone() const -> std::unique_ptr<iuse_actor>
{
    return std::make_unique<salvage_actor>( *this );
}

auto salvage_actor::use( player &p, item &it, bool t, const tripoint & ) const -> int
{
    if( t ) {
        return 0;
    }

    auto item_loc = game_menus::inv::salvage( p, this );
    if( !item_loc ) {
        add_msg( _( "Never mind." ) );
        return 0;
    }

    if( !try_to_cut_up( p, *item_loc.get_item() ) ) {
        // Messages should have already been displayed.
        return 0;
    }

    return cut_up( p, it, item_loc );
}

auto salvage_actor::time_to_cut_up( const item &it ) const -> int
{
    const auto &made_of = it.made_of();
    if( made_of.empty() ) {
        return 0;
    }

    const units::mass average_material_weight = total_material_weight( made_of ) / made_of.size();
    const int count = it.weight() / average_material_weight;
    return moves_per_part * count;
}

auto salvage_actor::valid_to_cut_up( const item &it ) const -> bool
{
    if( it.is_null() ) {
        return false;
    }
    // There must be some historical significance to these items.
    if( !it.is_salvageable() ) {
        return false;
    }
    if( !it.only_made_of( material_whitelist ) ) {
        return false;
    }
    if( !it.contents.empty() ) {
        return false;
    }
    if( it.weight() < minimal_weight_to_cut( it.made_of() ) ) {
        return false;
    }

    return true;
}

// it here is the item that is a candidate for being chopped up.
// This is the former valid_to_cut_up with all the messages and queries
auto salvage_actor::try_to_cut_up( player &p, item &it ) const -> bool
{
    int pos = p.get_item_position( &it );

    if( it.is_null() ) {
        add_msg( m_info, _( "You do not have that item." ) );
        return false;
    }
    // There must be some historical significance to these items.
    if( !it.is_salvageable() ) {
        add_msg( m_info, _( "Can't salvage anything from %s." ), it.tname() );
        if( recipe_dictionary::get_uncraft( it.typeId() ) ) {
            add_msg( m_info, _( "Try disassembling the %s instead." ), it.tname() );
        }
        return false;
    }

    if( !it.only_made_of( material_whitelist ) ) {
        add_msg( m_info, _( "The %s is made of material that cannot be cut up." ), it.tname() );
        return false;
    }
    if( !it.contents.empty() ) {
        add_msg( m_info, _( "Please empty the %s before cutting it up." ), it.tname() );
        return false;
    }
    if( it.weight() < minimal_weight_to_cut( it.made_of() ) ) {
        add_msg( m_info, _( "The %s is too small to salvage material from." ), it.tname() );
        return false;
    }
    // Softer warnings at the end so we don't ask permission and then tell them no.
    if( p.is_wielding( it ) ) {
        if( !query_yn( _( "You are wielding that, are you sure?" ) ) ) {
            return false;
        }
    } else if( pos == INT_MIN ) {
        // Not in inventory
        return true;
    } else if( pos < -1 ) {
        if( !query_yn( _( "You're wearing that, are you sure?" ) ) ) {
            return false;
        }
    }

    return true;
}

// function returns charges from it during the cutting process of the *cut.
// it cuts
// cut gets cut
auto salvage_actor::cut_up( player &p, item &it, item_location &cut ) const -> int
{
    const bool filthy = cut.get_item()->is_filthy();
    // This is the value that tracks progress, as we cut pieces off, we reduce this number.
    units::mass remaining_weight = cut.get_item()->weight();
    // Chance of us losing a material component to entropy.
    /** @EFFECT_FABRICATION reduces chance of losing components when cutting items up */
    int entropy_threshold = std::max( 5, 10 - p.get_skill_level( skill_fabrication ) );
    // What material components can we get back?
    std::vector<material_id> cut_material_components = cut.get_item()->made_of();
    // What materials do we salvage (ids and counts).
    std::map<itype_id, int> materials_salvaged;

    // Final just in case check (that perhaps was not done elsewhere);
    if( cut.get_item() == &it ) {
        add_msg( m_info, _( "You can not cut the %s with itself." ), it.tname() );
        return 0;
    }
    if( !cut.get_item()->contents.empty() ) {
        // Should have been ensured by try_to_cut_up
        debugmsg( "tried to cut a non-empty item %s", cut.get_item()->tname() );
        return 0;
    }

    // Not much practice, and you won't get very far ripping things up.
    p.practice( skill_fabrication, rng( 0, 5 ), 1 );

    // Higher fabrication, less chance of entropy, but still a chance.
    if( rng( 1, 10 ) <= entropy_threshold ) {
        remaining_weight *= 0.99;
    }
    // Fail dex roll, potentially lose more parts.
    /** @EFFECT_DEX randomly reduces component loss when cutting items up */
    if( dice( 3, 4 ) > p.dex_cur ) {
        remaining_weight *= 0.95;
    }
    // If more than 1 material component can still be salvaged,
    // chance of losing more components if the item is damaged.
    // If the item being cut is not damaged, no additional losses will be incurred.
    if( cut.get_item()->damage() > 0 ) {
        float component_success_chance = std::min( std::pow( 0.8, cut.get_item()->damage_level( 4 ) ),
                                         1.0 );
        remaining_weight *= component_success_chance;
    }

    // Essentially we round-robbin through the components subtracting mass as we go.
    std::map<units::mass, itype_id> weight_to_item_map;
    for( const material_id &material : cut_material_components ) {
        if( const std::optional<itype_id> id = material->salvaged_into() ) {
            materials_salvaged[*id] = 0;
            weight_to_item_map[ item( *id, calendar::turn_zero, item::solitary_tag{} ).weight() ] = *id;
        }
    }
    while( remaining_weight > 0_gram && !weight_to_item_map.empty() ) {
        units::mass components_weight = std::accumulate( weight_to_item_map.begin(),
                                        weight_to_item_map.end(), 0_gram, []( const units::mass & a,
        const std::pair<units::mass, itype_id> &b ) {
            return a + b.first;
        } );
        if( components_weight > 0_gram && components_weight <= remaining_weight ) {
            int count = remaining_weight / components_weight;
            for( std::pair<units::mass, itype_id> mat_pair : weight_to_item_map ) {
                materials_salvaged[mat_pair.second] += count;
            }
            remaining_weight -= components_weight * count;
        }
        weight_to_item_map.erase( std::prev( weight_to_item_map.end() ) );
    }

    add_msg( m_info, _( "You try to salvage materials from the %s." ),
             cut.get_item()->tname() );

    item_location::type cut_type = cut.where();
    tripoint pos = cut.position();

    // Clean up before removing the item.
    remove_ammo( *cut.get_item(), p );
    // Original item has been consumed.
    cut.remove_item();
    // Force an encumbrance update in case they were wearing that item.
    p.reset_encumbrance();

    map &here = get_map();
    for( const auto &salvaged : materials_salvaged ) {
        itype_id mat_name = salvaged.first;
        int amount = salvaged.second;
        item result( mat_name, calendar::turn );
        if( amount > 0 ) {
            // Time based on number of components.
            p.moves -= moves_per_part;
            add_msg( m_good, vgettext( "Salvaged %1$i %2$s.", "Salvaged %1$i %2$s.", amount ),
                     amount, result.display_name( amount ) );
            if( filthy ) {
                result.set_flag( "FILTHY" );
            }
            if( cut_type == item_location::type::character ) {
                p.i_add_or_drop( result, amount );
            } else {
                for( int i = 0; i < amount; i++ ) {
                    here.spawn_an_item( pos.xy(), result, amount, 0 );
                }
            }
        } else {
            add_msg( m_bad, _( "Could not salvage a %s." ), result.display_name() );
        }
    }
    // No matter what, cutting has been done by the time we get here.
    return cost >= 0 ? cost : it.ammo_required();
}

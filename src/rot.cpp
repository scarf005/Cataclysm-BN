#include "rot.h"

#include "item.h"
#include "locations.h"
#include "map/map.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_part.h"
#include "vehicle/vpart_position.h"

namespace rot::temp
{

auto for_tile( const tile_flags &flags ) -> temperature_flag
{
    if( flags.root_cellar ) {
        return temperature_flag::TEMP_ROOT_CELLAR;
    }
    if( flags.freezer ) {
        return temperature_flag::TEMP_FREEZER;
    }
    if( flags.fridge ) {
        return temperature_flag::TEMP_FRIDGE;
    }

    return temperature_flag::TEMP_NORMAL;
}

auto for_location( const map &m, const item &loc ) -> temperature_flag
{
    if( !loc.has_position() ) {
        return temperature_flag::TEMP_NORMAL;
    }

    if( const auto *vehicle_loc = loc.vehicle_location() ) {
        return vehicle_loc->storage_temperature();
    }

    switch( loc.where() ) {
        case item_location_type::character:
            return temperature_flag::TEMP_NORMAL;
        case item_location_type::monster:
            return temperature_flag::TEMP_NORMAL;
        case item_location_type::map: {
            const auto pos = loc.bub_pos();
            return for_tile( {
                .root_cellar = m.ter( pos ) == t_rootcellar,
                .fridge = m.has_flag_furn( TFLAG_FRIDGE, pos ),
                .freezer = m.has_flag_furn( TFLAG_FREEZER, pos ),
            } );
        }
        case item_location_type::vehicle:
            return temperature_flag::TEMP_NORMAL;
        case item_location_type::container: {
            const auto parent = loc.parent_item();
            if( parent == nullptr ) {
                return temperature_flag::TEMP_NORMAL;
            }
            return for_location( m, *parent );
        }
        default:
            debugmsg( "Invalid item location %d", static_cast<int>( loc.where() ) );
            return temperature_flag::TEMP_NORMAL;
    }
}

auto for_part( const vehicle &veh, const size_t part_index,
               const bool engine_heater_is_on ) -> temperature_flag
{
    const auto &part = veh.cpart( part_index );
    const auto &info = part.info();
    if( part.enabled && info.has_flag( VPFLAG_FREEZER ) ) {
        return temperature_flag::TEMP_FREEZER;
    }

    if( part.enabled && info.has_flag( VPFLAG_FRIDGE ) ) {
        return temperature_flag::TEMP_FRIDGE;
    }

    if( engine_heater_is_on ) {
        return temperature_flag::TEMP_HEATER;
    }

    return temperature_flag::TEMP_NORMAL;
}

} // namespace rot::temp

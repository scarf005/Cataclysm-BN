#include "catalua_coord.h"

#include <cstdio>

namespace cata::detail::lua_coords
{

auto read_point_coord_from_lua( const point_coord_lua_read_options &options ) -> bool
{
    const auto obj = sol::stack::get<sol::object>( options.L, options.index );
    if( obj.is<lua_point_coord>() ) {
        const auto coord = obj.as<lua_point_coord>();
        if( coord.origin == options.origin && coord.scale == options.scale ) {
            *options.out = coord.raw;
            return true;
        }
        return false;
    }
    return false;
}

auto read_tripoint_coord_from_lua( const tripoint_coord_lua_read_options &options ) -> bool
{
    const auto obj = sol::stack::get<sol::object>( options.L, options.index );
    if( options.origin == coords::origin::bubble && options.scale == coords::scale::map_square ) {
        std::fprintf( stderr,
                      "coord_probe index=%d top=%d type=%d proxy=%d expected_origin=%d expected_scale=%d\n",
                      options.index, lua_gettop( options.L ), static_cast<int>( sol::type_of( options.L,
                              options.index ) ),
                      obj.is<lua_tripoint_coord>(), static_cast<int>( options.origin ),
                      static_cast<int>( options.scale ) );
    }
    if( obj.is<lua_tripoint_coord>() ) {
        const auto coord = obj.as<lua_tripoint_coord>();
        if( options.origin == coords::origin::bubble && options.scale == coords::scale::map_square ) {
            std::fprintf( stderr, "coord_probe actual_origin=%d actual_scale=%d raw=%d,%d,%d\n",
                          static_cast<int>( coord.origin ), static_cast<int>( coord.scale ), coord.raw.x, coord.raw.y,
                          coord.raw.z );
        }
        if( coord.origin == options.origin && coord.scale == options.scale ) {
            *options.out = coord.raw;
            return true;
        }
        return false;
    }
    return false;
}

auto push_raw_point( lua_State *L, const point &raw ) -> int
{
    return sol::stack::push( L, raw );
}

auto push_raw_tripoint( lua_State *L, const tripoint &raw ) -> int
{
    return sol::stack::push( L, raw );
}

} // namespace cata::detail::lua_coords

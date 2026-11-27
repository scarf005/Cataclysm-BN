#include "avatar.h"
#include "calendar.h"
#include "cata_variant.h"
#include "catalua_bindings_game_internal.h"
#include "catalua_bindings_utils.h"
#include "catalua_impl.h"
#include "catalua_luna_doc.h"
#include "character_id.h"
#include "debug.h"
#include "enum_conversions.h"
#include "event.h"
#include "event_bus.h"
#include "explosion.h"
#include "game.h"
#include "game_inventory.h"
#include "item.h"
#include "map/map.h"
#include "monster.h"
#include "output.h"
#include "sounds.h"
#include "text_snippets.h"
#include "timed_event.h"

#include <optional>
#include <stdexcept>
#include <string>

template <> struct enum_traits<timed_event_type> {
    static constexpr auto last = NUM_TIMED_EVENT_TYPES;
};

namespace io {
template <> auto enum_to_string<timed_event_type>(timed_event_type data) -> std::string {
    switch (data) {
            // clang-format off
        case TIMED_EVENT_NULL: return "null";
        case TIMED_EVENT_HELP: return "help";
        case TIMED_EVENT_WANTED: return "wanted";
        case TIMED_EVENT_ROBOT_ATTACK: return "robot_attack";
        case TIMED_EVENT_SPAWN_WYRMS: return "spawn_wyrms";
        case TIMED_EVENT_AMIGARA: return "amigara";
        case AMIGARA_WHISPERS: return "amigara_whispers";
        case TIMED_EVENT_ROOTS_DIE: return "roots_die";
        case TIMED_EVENT_TEMPLE_OPEN: return "temple_open";
        case TIMED_EVENT_TEMPLE_FLOOD: return "temple_flood";
        case TIMED_EVENT_TEMPLE_SPAWN: return "temple_spawn";
        case TIMED_EVENT_DIM: return "dim";
        case TIMED_EVENT_ARTIFACT_LIGHT: return "artifact_light";
        // clang-format on
        case NUM_TIMED_EVENT_TYPES:
            break;
    }
    debugmsg("Invalid timed_event_type");
    abort();
}
} // namespace io

namespace cata::detail {

auto reg_game_api_effects(luna::userlib& lib) -> void {
    DOC("Makes a sound that creatures can hear.  ");
    DOC("Takes a table with keys:  ");
    DOC("* `origin` (TripointBubMs)  ");
    DOC("* `volume` (integer): Loudness at the origin.  ");
    DOC("* `category` (string, optional): For example `alarm` or `music`. Defaults to `background`.  ");
    DOC("* `description` (string, optional): Message shown when the player hears it.  ");
    DOC("* `id` and `variant` (string, optional): Sound effect to play.");
    luna::set_fx(lib, "sound", [](const sol::table& opts) -> void {
        sounds::sound(sound_event{
            .volume = opts.get<short>("volume"),
            .origin = opts.get<tripoint_bub_ms>("origin"),
            .category = io::string_to_enum<sounds::sound_t>(
                opts.get_or<std::string>("category", "background")),
            .description = opts.get_or<std::string>("description", ""),
            .id = opts.get_or<std::string>("id", ""),
            .variant = opts.get_or<std::string>("variant", "default"),
        });
    });

    DOC("Makes an explosion.  ");
    DOC("Takes a table with keys:  ");
    DOC("* `pos` (TripointBubMs)  ");
    DOC("* `power` (number)  ");
    DOC("* `source` (Creature, optional): Who caused it.  ");
    DOC("* `factor` (number, optional): Defaults to 0.8.  ");
    DOC("* `fire` (boolean, optional): Defaults to false.  ");
    DOC("* `casing_mass` (integer, optional): Mass of the shrapnel casing. Defaults to 0, no shrapnel.");
    luna::set_fx(lib, "explosion", [](const sol::table& opts) -> void {
        explosion_handler::explosion(
            opts.get<tripoint_bub_ms>("pos"), opts.get_or<Creature*>("source", nullptr),
            opts.get<float>("power"), opts.get_or("factor", 0.8f), opts.get_or("fire", false),
            opts.get_or("casing_mass", 0));
    });

    DOC("Starts a resonance cascade at a position.");
    luna::set_fx(lib, "resonance_cascade", &explosion_handler::resonance_cascade);

    DOC("Schedules a timed event by id, for example `amigara`.");
    DOC("`wanted` takes a faction id as `strength` (0 for none) and the target submap as `where`.");
    luna::set_fx(
        lib, "add_timed_event",
        [](const std::string& id, const time_point& when, sol::optional<int> strength,
           sol::optional<tripoint_abs_sm> where) -> void {
            const auto type = io::string_to_enum<timed_event_type>(id);
            if (where) {
                g->timed_events.add(type, when, strength.value_or(-1), *where);
            } else {
                g->timed_events.add(type, when, strength.value_or(-1));
            }
        });

    DOC("Returns true if a timed event of this id is queued.");
    luna::set_fx(lib, "timed_event_queued", [](const std::string& id) -> bool {
        return g->timed_events.queued(io::string_to_enum<timed_event_type>(id));
    });

    DOC("Sends a game event, for example `opens_portal`.");
    DOC("Events with data fields take a table of them: `gapi.send_event(\"triggers_alarm\", { character = id })`.");
    DOC("Each field is a CharacterId, a string, an integer or a boolean. Pass string ids as strings.");
    luna::set_fx(
        lib, "send_event", [](const std::string& id, sol::optional<sol::table> data) -> void {
            const auto type = io::string_to_enum<event_type>(id);
            auto fields = cata::event::data_type{};
            for (const auto& [name, field_type] : cata::event::get_fields(type)) {
                const auto value = data ? (*data)[name].get<sol::object>() : sol::object{};
                const auto text = [&]() -> std::optional<std::string> {
                    switch (value.get_type()) {
                        case sol::type::string:
                            return value.as<std::string>();
                        case sol::type::number:
                            return std::to_string(value.as<int>());
                        case sol::type::boolean:
                            return value.as<bool>() ? "true" : "false";
                        default:
                            if (value.is<character_id>()) {
                                return std::to_string(value.as<character_id>().get_value());
                            }
                            return std::nullopt;
                    }
                }();
                if (!text) {
                    throw std::runtime_error("send_event '" + id + "' needs field '" + name + "'");
                }
                fields.emplace(name, cata_variant::from_string(field_type, std::string(*text)));
            }
            g->events().send(cata::event(type, calendar::turn, std::move(fields)));
        });

    DOC("Asks a yes or no question in a popup. No is selected first. Returns true for yes.");
    luna::set_fx(lib, "query_yn", [](const std::string& text) -> bool { return query_yn(text); });

    DOC("Places a monster on a random free point within `radius` of `center`. Returns it or nil.");
    luna::set_fx(
        lib, "place_monster_within",
        [](const mtype_id& id, const tripoint_bub_ms& center, int radius) -> monster* {
            return g->place_critter_within(id, get_map().points_in_radius(center, radius));
        });

    DOC("Returns a random translated snippet from a category, or nil if the category is empty.");
    DOC("With a seed, the same seed gives the same snippet in one game session.");
    luna::set_fx(
        lib, "random_snippet",
        [](const std::string& category, sol::optional<int> seed) -> sol::optional<std::string> {
            const auto snippet =
                seed ? SNIPPET.random_from_category(category, static_cast<unsigned int>(*seed))
                     : SNIPPET.random_from_category(category);
            if (!snippet) { return sol::nullopt; }
            return snippet->translated();
        });

    DOC("Fades out the sound playing on an audio channel over `duration` milliseconds.");
    luna::set_fx(lib, "fade_audio_channel", &sfx::fade_audio_channel);

    DOC("Removes a monster from the game without killing it, so it leaves no corpse or loot.");
    luna::set_fx(lib, "remove_monster", [](const monster& critter) -> void {
        g->remove_zombie(critter);
    });

    DOC("Height of the game terminal in rows.");
    luna::set_fx(lib, "get_terminal_height", []() -> int { return TERMY; });

    DOC("Opens a menu of the player's inventory items that pass `filter` and returns the chosen item or nil.");
    luna::set_fx(
        lib, "choose_inventory_item",
        [](const std::string& title, const sol::protected_function& filter) -> item* {
            return game_menus::inv::titled_filter_menu(
                [&filter](const item& it) {
                    auto res = filter(const_cast<item*>(&it));
                    check_func_result(res);
                    return res.get<bool>();
                },
                g->u, title);
        });
}

} // namespace cata::detail

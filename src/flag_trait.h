#pragma once
#ifndef CATA_SRC_FLAG_TRAIT_H
#define CATA_SRC_FLAG_TRAIT_H

#include <set>
#include <string>

#include "translations.h"
#include "type_id.h"
class JsonObject;

class json_trait_flag
{
        friend class DynamicDataLoader;
        friend class generic_factory<json_trait_flag>;

    public:
        // used by generic_factory
        trait_flag_str_id id = trait_flag_str_id::NULL_ID();
        bool was_loaded = false;

        json_trait_flag() = default;

        /** Fetches flag definition (or null flag if not found) */
        static auto get( const std::string &id ) -> const json_trait_flag&;

        /** Is this a valid (non-null) flag */
        operator bool() const;

        auto check() const -> void;

        /** true, if flags were loaded */
        static auto is_ready() -> bool;

        static auto get_all() -> const std::vector<json_trait_flag> &;

    private:
        std::set<trait_flag_str_id> conflicts_;

        /** Load flag definition from JSON */
        void load( const JsonObject &jo, const std::string &src );

        /** Load all flags from JSON */
        static auto load_all( const JsonObject &jo, const std::string &src ) -> void;

        /** finalize */
        static auto finalize_all() -> void;

        /** Check consistency of all loaded flags */
        static auto check_consistency() -> void;

        /** Clear all loaded flags (invalidating any pointers) */
        static auto reset() -> void;
};

#endif // CATA_SRC_FLAG_TRAIT_H

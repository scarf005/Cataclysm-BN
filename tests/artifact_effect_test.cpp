#include "avatar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_factory.h"
#include "player_helpers.h"
#include "state_helpers.h"

TEST_CASE("wielded_artifact_effect_is_found_only_when_an_item_type_has_it", "[artifact]") {
    clear_all_state();
    auto& guy = get_avatar();
    clear_character(guy, true);

    // fake.json "clairvoyance_plus_rod" has AEP_CLAIRVOYANCE_PLUS when wielded. No core item has
    // AEP_INVISIBLE.
    REQUIRE(item_controller->has_passive_artifact_effect(AEP_CLAIRVOYANCE_PLUS));
    REQUIRE_FALSE(item_controller->has_passive_artifact_effect(AEP_INVISIBLE));

    CHECK_FALSE(guy.has_artifact_with(AEP_CLAIRVOYANCE_PLUS));
    guy.wield(item::spawn("clairvoyance_plus_rod"));
    CHECK(guy.has_artifact_with(AEP_CLAIRVOYANCE_PLUS));
    CHECK_FALSE(guy.has_artifact_with(AEP_INVISIBLE));
}

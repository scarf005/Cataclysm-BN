#if defined(TILES) && defined(DYNAMIC_ATLAS)

#    include "cata_tiles.h"
#    include "catch/catch.hpp"
#    include "path_info.h"
#    include "sdl_wrappers.h"
#    include "sdltiles.h"
#    include "tile_helpers.h"

#    include <vector>

// Checks that each sprite of a sheet without a transparency color lands in the atlas intact.
// HitButton_iso.png has 16x20 sprites and no color key, so the loader copies each sprite straight
// from the sheet. The software renderer of the fixture puts each sprite on its own atlas sheet,
// so each lookup also has to find the right one of about 2500 sheets.
// Atlas readback premultiplies alpha, so compare alpha for each pixel and color for opaque ones.
TEST_CASE("tileset_sprites_keep_the_pixels_of_an_unkeyed_sheet", "[tiles]") {
    tile_context_fixture fixture(/*alias_overmap_context=*/true);
    if (!fixture.valid()) {
        WARN("skipped: this environment cannot set up a headless tile context");
        return;
    }
    tilecontext->load_tileset(
        "HitButton_iso", std::vector<mod_id>{}, /*precheck=*/false, /*force=*/true,
        /*pump_events=*/false);
    const auto* const ts = tilecontext->current_tileset();
    REQUIRE(ts != nullptr);
    ts->ensure_readback_loaded();

    const auto sheet = load_image(
        (PATH_INFO::gfxdir() + "HitButton_iso/HitButton_iso.png").c_str());
    const int sprite_w = 16;
    const int sprite_h = 20;
    const int columns = sheet->w / sprite_w;
    const int sprite_count = columns * (sheet->h / sprite_h);
    int differing = 0;
    for (int index = 0; index < sprite_count; ++index) {
        const auto [found, surf, rect] = ts->get_sprite_surface(index);
        REQUIRE(found);
        REQUIRE(rect.w == sprite_w);
        REQUIRE(rect.h == sprite_h);
        const int sheet_x = (index % columns) * sprite_w;
        const int sheet_y = (index / columns) * sprite_h;
        for (int y = 0; y < sprite_h; ++y) {
            for (int x = 0; x < sprite_w; ++x) {
                auto got = SDL_Color{};
                auto want = SDL_Color{};
                SDL_ReadSurfacePixel(surf, rect.x + x, rect.y + y, &got.r, &got.g, &got.b, &got.a);
                SDL_ReadSurfacePixel(
                    sheet.get(), sheet_x + x, sheet_y + y, &want.r, &want.g, &want.b, &want.a);
                const bool same_color = got.r == want.r && got.g == want.g && got.b == want.b;
                if (got.a != want.a || (want.a == 255 && !same_color)) { ++differing; }
            }
        }
    }
    CHECK(differing == 0);
}

#endif // TILES && DYNAMIC_ATLAS

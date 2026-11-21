#if defined(TILES)

#    include "cata_tiles.h"
#    include "catch/catch.hpp"
#    include "options.h"
#    include "sdl_wrappers.h"
#    include "sdltiles.h"
#    include "tile_helpers.h"
#    include "tileset_loading.h"

#    include <SDL3/SDL.h>
#    include <algorithm>
#    include <chrono>
#    include <cstdio>
#    include <cstdlib>
#    include <functional>
#    include <string>
#    include <thread>
#    include <vector>

namespace {

auto seconds_of(const std::function<void()>& f) -> double {
    const auto start = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

auto report(const std::string& name, const std::vector<double>& runs) -> void {
    std::string all;
    for (const auto r : runs) { all += " " + std::to_string(r); }
    std::printf("BENCH %s min=%.3f s runs:%s\n", name.c_str(), *std::ranges::min_element(runs), all.c_str());
    std::fflush(stdout);
}

} // namespace

// Times tileset loading. Run on CI only: BENCH_RENDERER picks the SDL renderer (default software).
TEST_CASE("tileset_load_bench", "[.][bench]") {
    const auto* const env_driver = std::getenv("BENCH_RENDERER");
    const std::string driver = env_driver == nullptr ? "software" : env_driver;
    SDL_Window_Ptr window;
    if (driver != "software" && !get_sdl_renderer()) {
        window.reset(SDL_CreateWindow("bench", 640, 480, SDL_WINDOW_HIDDEN));
        REQUIRE(window);
        auto renderer = SDL_Renderer_Ptr(SDL_CreateRenderer(window.get(), driver.c_str()));
        if (!renderer) {
            WARN("cannot create renderer " << driver << ": " << SDL_GetError());
            return;
        }
        set_sdl_renderer(std::move(renderer));
    }
    {
        tile_context_fixture fixture(/*alias_overmap_context=*/false);
        REQUIRE(fixture.valid());
        std::printf("BENCH renderer=%s\n", SDL_GetRendererName(get_sdl_renderer().get()));
        const std::vector<mod_id> no_mods;
        const auto load = [&](cata_tiles& ctx, const std::string& id) {
            ctx.load_tileset(id, no_mods, /*precheck=*/false, /*force=*/true, /*pump_events=*/false);
        };
        for (const std::string id : {"UNDEAD_PEOPLE_BASE", "UltimateCataclysm"}) {
            std::vector<double> loads;
            std::vector<double> reports;
            for (int i = 0; i < 3; ++i) {
                loads.push_back(seconds_of([&] { load(*tilecontext, id); }));
                reports.push_back(seconds_of([&] { tilecontext->do_tile_loading_report([](const std::string&) {}); }));
            }
            report("load " + driver + " " + id, loads);
            report("report " + driver + " " + id, reports);
        }
        // Game start with a separate overmap tileset: images decode ahead while game data loads
        // (stood in for by a 3 s wait), then the map and the overmap tilesets load.
        auto& tiles_opt = get_options().get_option("TILES");
        auto& overmap_opt = get_options().get_option("OVERMAP_TILES");
        const auto saved_tiles = tiles_opt.getValue();
        const auto saved_overmap = overmap_opt.getValue();
        tiles_opt.setValue("UltimateCataclysm");
        overmap_opt.setValue("UNDEAD_PEOPLE_BASE");
        std::vector<double> starts;
        for (int i = 0; i < 3; ++i) {
            load(*tilecontext, "ASCIITiles");
            load(*overmap_tilecontext, "ASCIITiles");
            tileset_loading::decode_images_ahead();
            std::this_thread::sleep_for(std::chrono::seconds(3));
            starts.push_back(seconds_of([&] {
                load(*tilecontext, "UltimateCataclysm");
                load(*overmap_tilecontext, "UNDEAD_PEOPLE_BASE");
            }));
            tileset_loading::discard_images_decoded_ahead();
        }
        report("start " + driver + " UltimateCataclysm+overmap UNDEAD_PEOPLE_BASE", starts);
        tiles_opt.setValue(saved_tiles);
        overmap_opt.setValue(saved_overmap);
    }
    if (window) { set_sdl_renderer(nullptr); }
}

#endif // TILES

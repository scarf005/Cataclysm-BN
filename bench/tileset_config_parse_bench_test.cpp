#if defined(TILES)

#    include "cata_tiles.h"
#    include "catch/catch.hpp"
#    include "filesystem.h"
#    include "json.h"
#    include "options.h"
#    include "path_info.h"
#    include "sdltiles.h"
#    include "tile_helpers.h"
#    include "tileset_loading.h"
#    include "utils/memory_stream.h"

#    include <algorithm>
#    include <chrono>
#    include <cstdio>
#    include <filesystem>
#    include <functional>
#    include <string>
#    include <vector>

namespace {

auto seconds_of(const std::function<void()>& f) -> double {
    const auto start = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

auto report(const std::string& name, const std::vector<double>& runs) -> void {
    std::string all;
    for (const auto r : runs) { all += " " + std::to_string(r * 1000.0); }
    std::printf("BENCH %s min=%.3f ms runs(ms):%s\n", name.c_str(), *std::ranges::min_element(runs) * 1000.0,
                all.c_str());
    std::fflush(stdout);
}

} // namespace

// Times the extra tile_config.json parse of tileset_loading::decode_images_ahead (read the file,
// parse it, collect "tiles-new" file names), per bundled tileset, and the main-thread time of the
// whole ahead pass. Run on CI only.
TEST_CASE("tileset_config_parse_bench", "[.][bench]") {
    for (const auto& conf : get_files_from_path("tile_config.json", PATH_INFO::gfxdir(), true)) {
        std::vector<double> runs;
        std::size_t sheets = 0;
        for (int i = 0; i < 10; ++i) {
            runs.push_back(seconds_of([&] {
                memory_istream config_file(read_entire_file(conf));
                JsonIn config_json(config_file);
                const auto config = config_json.get_object();
                config.allow_omitted_members();
                std::vector<std::string> files;
                if (config.has_array("tiles-new")) {
                    for (const JsonObject& part : config.get_array("tiles-new")) {
                        part.allow_omitted_members();
                        files.push_back(part.get_string("file"));
                    }
                }
                sheets = files.size();
            }));
        }
        std::printf("BENCH file %s size=%lld bytes sheets=%zu\n", conf.c_str(),
                    static_cast<long long>(std::filesystem::file_size(conf)), sheets);
        report("parse " + conf, runs);
    }

    tile_context_fixture fixture(/*alias_overmap_context=*/false);
    REQUIRE(fixture.valid());
    auto& tiles_opt = get_options().get_option("TILES");
    const auto saved_tiles = tiles_opt.getValue();
    const std::vector<mod_id> no_mods;
    for (const std::string id : {"UNDEAD_PEOPLE_BASE", "UltimateCataclysm"}) {
        tilecontext->load_tileset("ASCIITiles", no_mods, /*precheck=*/false, /*force=*/true, /*pump_events=*/false);
        tiles_opt.setValue(id);
        std::vector<double> runs;
        for (int i = 0; i < 5; ++i) {
            runs.push_back(seconds_of([] { tileset_loading::decode_images_ahead(); }));
            tileset_loading::discard_images_decoded_ahead();
        }
        report("ahead pass main thread " + id, runs);
    }
    tiles_opt.setValue(saved_tiles);
}

#endif // TILES

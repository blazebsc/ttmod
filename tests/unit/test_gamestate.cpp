#include "ttmod/gamestate.hpp"
#include "ttmod/logmap.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

int main() {
    using ttmod::parse_episode;
    assert(parse_episode("mcsm_pc_minecraft106_ms.ttarch2") == 6);
    assert(parse_episode("_resdesc_50_minecraft102_data") == 2);
    assert(parse_episode("mcsm_pc_minecraft101_ms.ttarch2") == 1);
    assert(parse_episode("mcsm_pc_minecraft108_voice.ttarch2") == 8);
    assert(parse_episode("mcsm_pc_menu_ms.ttarch2") == 0);
    assert(parse_episode("mcsm_pc_boot_data.ttarch2") == 0);
    assert(parse_episode("_resdesc_50_boot.lua") == 0);
    assert(parse_episode("mcsm_pc_minecraft109_x.ttarch2") == 0); // out of range
    assert(parse_episode("x") == 0);

    using ttmod::is_save_file;
    assert(is_save_file("prefs.prop"));
    assert(is_save_file("h:/d/session_1.estore"));
    assert(is_save_file("elfdl.prop"));
    assert(!is_save_file("x.prop"));
    assert(!is_save_file("session_x.txt"));

    ttmod::GameTracker t;
    t.feed("resdesc", "h:/g/archives/_resdesc_50_boot.lua");
    t.feed("archive", "h:/g/archives/mcsm_pc_minecraft106_ms.ttarch2");
    t.feed("archive", "h:/g/archives/mcsm_pc_minecraft106_data.ttarch2"); // dup ep
    t.feed("archive", "h:/g/archives/mcsm_pc_minecraft102_ms.ttarch2");
    t.feed("other", "c:/u/d/telltale games/minecraft - story mode/prefs.prop");
    t.feed("other", "c:/u/d/telltale games/minecraft - story mode/session_1.estore");
    t.feed("other", "c:/w/x.dll");
    auto s = t.snapshot();
    assert(s.episodes_seen.size() == 2 && s.episodes_seen[0] == 6 && s.episodes_seen[1] == 2);
    assert(s.archives_opened == 3 && s.resdesc_opened == 1);
    assert(s.saves_observed == 2 && s.others_opened == 1);
    assert(s.save_dir == "c:/u/d/telltale games/minecraft - story mode");

    // Single classifier: classify_category == classify_path + save override.
    using ttmod::classify_category;
    assert(classify_category("h:/g/archives/_resdesc_50_boot.lua") == "resdesc");
    assert(classify_category("h:/g/archives/mcsm_pc_menu_ms.ttarch2") == "archive");
    assert(classify_category("c:/w/x.dll") == "other");
    assert(classify_category("c:/u/d/prefs.prop") == "save");
    assert(classify_category("c:/u/d/session_1.estore") == "save");

    // Live tracker and offline replay agree path-for-path. The feed's category
    // argument is deliberately wrong here: the single classifier wins, so a
    // disagreeing caller cannot skew live counts away from the replay.
    {
        std::vector<std::string> paths = {
            "h:/g/archives/_resdesc_50_boot.lua",
            "h:/g/archives/mcsm_pc_menu_ms.ttarch2",
            "h:/g/archives/mcsm_pc_menu_ms.ttarch2",
            "c:/u/documents/telltale games/x/prefs.prop",
            "c:/w/x.dll",
        };
        ttmod::GameTracker t2;
        for (auto& p : paths) t2.feed("WRONG", p);
        auto s2 = t2.snapshot();
        auto m2 = ttmod::map_from_paths(paths);
        assert(s2.archives_opened == m2.by_category["archive"]);
        assert(s2.resdesc_opened == m2.by_category["resdesc"]);
        assert(s2.saves_observed == m2.by_category["save"]);
        assert(s2.others_opened == m2.by_category["other"]);
    }

    // Structured snapshot rendering (primary detach artifact): exact bytes.
    {
        ttmod::GameSnapshot js;
        js.episodes_seen = {6, 2};
        js.archives_opened = 3;
        js.resdesc_opened = 1;
        js.saves_observed = 2;
        js.others_opened = 1;
        js.save_dir = "c:/x";
        char b[512];
        int n = ttmod::format_snapshot_json(js, b, sizeof b);
        assert(n > 0);
        assert(std::string(b) ==
               "{\"episodes\":[6,2],\"archives\":3,\"resdesc\":1,"
               "\"saves\":2,\"others\":1,\"save_dir\":\"c:/x\"}\r\n");
        assert(ttmod::format_snapshot_json(js, nullptr, 0) == -1);
        char tiny[8];
        assert(ttmod::format_snapshot_json(js, tiny, sizeof tiny) > 0); // truncates, NUL-ends
    }
    std::puts("gamestate: all asserts passed");
    return 0;
}

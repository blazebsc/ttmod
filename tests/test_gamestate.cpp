#include "ttmod/gamestate.hpp"
#include <cassert>
#include <cstdio>

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
    std::puts("gamestate: all asserts passed");
    return 0;
}

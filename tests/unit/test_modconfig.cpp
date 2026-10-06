#include "ttmod/manifest.hpp"
#include "ttmod/modconfig.hpp"
#include "ttmod/modstate.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

static ttmod::ModManifest with_config() {
    auto r = ttmod::parse_manifest(
        "{\"id\":\"demo.config\",\"version\":\"1.2.0\",\"api\":1,"
        "\"name\":\"Demo Config\",\"description\":\"Proof mod\","
        "\"games\":[\"minecraft-story-mode:s1\"],"
        "\"config\":["
        "{\"key\":\"greeting\",\"type\":\"string\",\"label\":\"Greeting\",\"default\":\"Hi\"},"
        "{\"key\":\"level\",\"type\":\"int\",\"label\":\"Level\",\"default\":3,\"min\":1,\"max\":9},"
        "{\"key\":\"fancy\",\"type\":\"bool\",\"label\":\"Fancy\",\"default\":true},"
        "{\"key\":\"mode\",\"type\":\"enum\",\"label\":\"Mode\",\"options\":[\"a\",\"b\"]}"
        "]}");
    assert(r.ok());
    return r.value();
}

int main() {
    // schema via manifest
    auto m = with_config();
    assert(m.presentation.name == "Demo Config" && m.presentation.description == "Proof mod");
    assert(m.presentation.config.size() == 4);
    assert(m.presentation.config[0].type == "string" && m.presentation.config[0].def_str == "Hi");
    assert(m.presentation.config[1].type == "int" && m.presentation.config[1].def_int == 3);
    assert(m.presentation.config[1].has_min && m.presentation.config[1].has_max);
    assert(m.presentation.config[2].type == "bool" && m.presentation.config[2].def_bool);
    assert(m.presentation.config[3].type == "enum" && m.presentation.config[3].options.size() == 2 &&
           m.presentation.config[3].def_str == "a"); // enum defaults to first option
    // strict schema rejections (manifest fails, mod still loadable? no:
    // manifest must stay valid -> schema errors fail the manifest)
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"config\":{}}").ok());
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"config\":[{\"key\":\"a\"}]}").ok());
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"config\":["
                                  "{\"key\":\"a\",\"type\":\"nope\",\"label\":\"A\"}]}")
                .ok());
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"config\":["
                                  "{\"key\":\"a\",\"type\":\"enum\",\"label\":\"A\"}]}")
                .ok()); // enum w/o options
    // color type: #RRGGBB only, at schema and at value level
    assert(ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"config\":["
                                 "{\"key\":\"c\",\"type\":\"color\",\"label\":\"C\","
                                 "\"default\":\"#E0A040\"}]}")
               .ok());
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"config\":["
                                  "{\"key\":\"c\",\"type\":\"color\",\"label\":\"C\","
                                  "\"default\":\"orange\"}]}")
                 .ok());
    // old manifests unaffected
    auto plain = ttmod::parse_manifest("{\"id\":\"p\",\"api\":1}").value();
    assert(plain.presentation.config.empty() && plain.presentation.name.empty());

    // file values + effective merge
    auto fr = ttmod::parse_config_file("{\"greeting\":\"Yo\",\"level\":7,\"fancy\":false,"
                                       "\"mode\":\"b\",\"unknown\":1}");
    assert(fr.ok());
    auto f = fr.value();
    auto eff = ttmod::config_effective(m.presentation.config, f);
    assert(eff["greeting"].s == "Yo" && eff["level"].i == 7 && !eff["fancy"].b &&
           eff["mode"].s == "b");
    // invalid file values fall back to defaults
    auto badr = ttmod::parse_config_file("{\"level\":99,\"mode\":\"zzz\",\"fancy\":\"yes\"}");
    assert(badr.ok()); // parses; values rejected at merge
    auto bad = badr.value();
    auto eff2 = ttmod::config_effective(m.presentation.config, bad);
    assert(eff2["level"].i == 3 && eff2["mode"].s == "a" && eff2["fancy"].b);
    // garbage file -> all defaults
    auto junk = ttmod::parse_config_file("{oops");
    assert(!junk.ok());
    auto eff3 = ttmod::config_effective(m.presentation.config, ttmod::ConfigFile{});
    assert(eff3["greeting"].s == "Hi");
    // float + int tolerance
    auto ffr = ttmod::parse_config_file("{\"level\":4.0}");
    assert(ffr.ok());
    auto ff = ffr.value();
    assert(ff.values["level"].type == ttmod::ConfigValue::Type::FLOAT);

    // color values: accepted only as #RRGGBB, rejected values fall back
    auto cm = ttmod::parse_manifest("{\"id\":\"c\",\"api\":1,\"config\":["
                                    "{\"key\":\"accent\",\"type\":\"color\",\"label\":\"A\","
                                    "\"default\":\"#E0A040\"}]}").value();
    assert(cm.presentation.config[0].type == "color");
    auto cok = ttmod::config_effective(
        cm.presentation.config, ttmod::parse_config_file("{\"accent\":\"#00FF80\"}").value());
    assert(cok["accent"].s == "#00FF80");
    auto cbad = ttmod::config_effective(
        cm.presentation.config, ttmod::parse_config_file("{\"accent\":\"lime\"}").value());
    assert(cbad["accent"].s == "#E0A040"); // bad file value -> default
    assert(ttmod::apply_config_value(cm.presentation.config, "{\"accent\":\"#E0A040\"}", "accent", "#123456")
               .ok());
    assert(!ttmod::apply_config_value(cm.presentation.config, "{\"accent\":\"#E0A040\"}", "accent", "123456")
                .ok());
    assert(!ttmod::apply_config_value(cm.presentation.config, "{\"accent\":\"#E0A040\"}", "accent", "#FFF")
                .ok());

    // serialization round-trip (deterministic)
    std::string s = ttmod::serialize_config(m.presentation.config, eff);
    assert(s == "{\"greeting\":\"Yo\",\"level\":7,\"fancy\":false,\"mode\":\"b\"}");
    auto f2r = ttmod::parse_config_file(s);
    assert(f2r.ok() && f2r.value().values["level"].i == 7);

    // Lua literal: escaped, typed, sequenced
    ttmod::MenuModSnapshot snap;
    snap.id = "demo.config";
    snap.name = "Demo \"Mod\"";
    snap.version = "1.2.0";
    snap.enabled = true;
    snap.schema = m.presentation.config;
    snap.values = eff;
    // hostile string: quotes, backslash, newline, control char
    snap.values["greeting"] = ttmod::ConfigValue::text(std::string("a\"b\\c\nd") + char(0x01) + "e");
    std::string lit = ttmod::build_menu_literal({snap}, 7);
    assert(lit.find("ttmod_menu={seq=7,mods={") == 0);
    assert(lit.find("name=\"Demo \\\"Mod\\\"\"") != std::string::npos);
    assert(lit.find("a\\\"b\\\\c\\nde\"") != std::string::npos); // \x01 stripped
    assert(lit.find("key=\"level\"") != std::string::npos);
    assert(lit.find("value=7") != std::string::npos);
    assert(lit.find("enabled=true") != std::string::npos);
    assert(lit.rfind("}}") == lit.size() - 2);
    // dump for the stock-Lua parse check (see test_menumods_ui.py tail).
    // Path must be creatable anywhere: CI has no /tmp/opencode, and a failed
    // fopen aborted the whole test with no useful message.
    const char* tmpdir = getenv("TTMOD_TEST_TMP");
    std::string litpath = std::string(tmpdir && *tmpdir ? tmpdir : "/tmp") +
                          "/ttmod_menu_literal_check.lua";
    FILE* lf = fopen(litpath.c_str(), "w");
    if (!lf) {
        fprintf(stderr, "test_modconfig: cannot write %s\n", litpath.c_str());
        return 1;
    }
    fwrite(lit.data(), 1, lit.size(), lf);
    fprintf(lf, "\nassert(ttmod_menu.seq==7 and #ttmod_menu.mods==1)\n");
    fclose(lf);

    // enable/disable transitions (pure text in/out)
    std::string st1 = ttmod::apply_enabled_change("", "demo.config", false);
    assert(st1.find("\"demo.config\"") != std::string::npos && st1.find("false") != std::string::npos);
    std::string st2 = ttmod::apply_enabled_change(st1, "demo.config", true);
    auto sf = ttmod::parse_state(st2);
    assert(sf.ok() && sf.value().enabled_for("demo.config", false));
    // garbage input tolerated, other entries preserved
    std::string st3 = ttmod::apply_enabled_change("{oops", "a.b", false);
    assert(ttmod::parse_state(st3).ok());

    // config value transitions
    auto sv = ttmod::apply_config_value(m.presentation.config, "", "fancy", "0");
    assert(sv.ok() && sv.value().find("\"fancy\":false") != std::string::npos);
    auto sv2 = ttmod::apply_config_value(m.presentation.config, sv.value(), "level", "9");
    assert(sv2.ok() && sv2.value().find("\"level\":9") != std::string::npos);
    // rejects: out-of-range, bad enum, bad int, unknown key
    assert(!ttmod::apply_config_value(m.presentation.config, "", "level", "99").ok());
    assert(!ttmod::apply_config_value(m.presentation.config, "", "level", "x").ok());
    assert(!ttmod::apply_config_value(m.presentation.config, "", "mode", "zzz").ok());
    assert(!ttmod::apply_config_value(m.presentation.config, "", "nope", "1").ok());
    assert(!ttmod::apply_config_value(m.presentation.config, "", "fancy", "maybe").ok());
    // empty string resets strings to default; unknown keys survive round-trip
    auto sv3 = ttmod::apply_config_value(m.presentation.config, "{\"greeting\":\"Yo\",\"zz\":5}", "greeting", "");
    assert(sv3.ok() && sv3.value().find("\"greeting\":\"Hi\"") != std::string::npos &&
           sv3.value().find("\"zz\":5") != std::string::npos);
    // float coercion accepts ints
    auto sv4 = ttmod::apply_config_value(m.presentation.config, "", "level", "4");
    assert(sv4.ok());

    std::puts("modconfig: all asserts passed");
    return 0;
}

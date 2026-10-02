#include "ttmod/detect.hpp"
#include "ttmod/profile.hpp"
#include "ttmod/log.hpp"
#include <cassert>
#include <cstdio>

int main() {
    // Known MCSM1 build selects supported profile (identity incl. FNV)
    ttmod::ExeInfo known;
    known.ok = true;
    known.machine = 0x014C;
    known.timestamp = 1463779093u;
    known.file_size = 12179904ull;
    known.opt_magic = 0x10B;
    known.fnv1a64 = 0xA11CD391555291BEull;
    auto p = ttmod::select_profile(known);
    assert(std::string(p.id) == "mcsm1_pc_x86");
    assert(p.season == 1 && std::string(p.status) == "supported");

    // Same shape but different bytes (CODEX crack) is NOT supported
    ttmod::ExeInfo codex = known;
    codex.fnv1a64 = 0x26B0BE1D74787BCDull;
    auto pc = ttmod::select_profile(codex);
    assert(std::string(pc.id) == "mcsm1_pc_x86_codex");
    assert(std::string(pc.status) == "unrecognized-build" && pc.season == 1);

    // ALI213 variant
    ttmod::ExeInfo ali = known;
    ali.file_size = 11724800ull;
    ali.fnv1a64 = 0xE507F1E444839F62ull;
    assert(std::string(ttmod::select_profile(ali).id) == "mcsm1_pc_x86_ali213");

    // Same shape, unknown bytes -> safe variant id, never supported
    ttmod::ExeInfo var = known;
    var.fnv1a64 = 0x1234ull;
    auto pv = ttmod::select_profile(var);
    assert(std::string(pv.id) == "mcsm1_pc_x86_variant");
    assert(std::string(pv.status) == "unrecognized-build");

    // Unknown x86 stays unknown, x64 is explicitly not-implemented
    ttmod::ExeInfo other;
    other.ok = true;
    other.machine = 0x014C;
    other.timestamp = 1;
    other.file_size = 2;
    assert(std::string(ttmod::select_profile(other).id) == "unknown");
    ttmod::ExeInfo x64;
    x64.ok = true;
    x64.machine = 0x8664;
    assert(std::string(ttmod::select_profile(x64).status) == "defined-not-implemented");
    ttmod::ExeInfo bad;
    bad.ok = false;
    assert(std::string(ttmod::select_profile(bad).id) == "unknown");

    // Logger writes expected lines
    const char* tmp = "/tmp/opencode_ttmod_logtest.log";
    std::remove(tmp);
    {
        ttmod::Logger log;
        assert(log.open(tmp));
        log.info("hello");
        log.warn("careful");
        log.error("boom");
    }
    FILE* f = fopen(tmp, "r");
    assert(f);
    char buf[1024] = {};
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    std::string s(buf, n);
    assert(s.find("[INFO] hello") != std::string::npos);
    assert(s.find("[WARN] careful") != std::string::npos);
    assert(s.find("[ERROR] boom") != std::string::npos);
    std::puts("profile+log: all asserts passed");
    return 0;
}

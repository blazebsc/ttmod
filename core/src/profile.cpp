#include "ttmod/profile.hpp"
#include "ttmod/detect.hpp"
#include "ttmod/log.hpp"
#include "ttmod/runtime.hpp"
#include "ttmod/version.hpp"

namespace ttmod {

GameProfile select_profile(const ExeInfo& e) {
    // MCSM1 builds. Identity = size + timestamp + FNV-1a64 (size+timestamp
    // alone CONFUSES crack-patched variants, e.g. CODEX). SHA256 pinned in
    // docs/games/mcsm1.md. Only the byte-exact known build is "supported";
    // everything else idles safely (M9).
    GameMatch m = match_game(e);
    GameProfile p;
    p.id = m.id;
    p.game = m.game;
    p.season = m.season;
    p.arch = m.arch;
    p.status = evaluate_support(m);
    return p;
}

GameMatch match_game(const ExeInfo& e) {
    GameMatch m;
    const bool mcsm1_shape =
        e.machine == 0x014C && e.timestamp == 1463779093u && e.opt_magic == 0x10B;
    m.fnv1a64 = e.fnv1a64;
    m.file_size = e.file_size;
    if (mcsm1_shape && e.file_size == 12179904ull && e.fnv1a64 == 0xA11CD391555291BEull) {
        m.id = "mcsm1_pc_x86";
        m.game = "minecraft-story-mode";
        m.season = 1;
        m.arch = Architecture::X86;
        return m;
    }
    if (mcsm1_shape && e.file_size == 11724800ull && e.fnv1a64 == 0xE507F1E444839F62ull) {
        m.id = "mcsm1_pc_x86_ali213"; // ALI213 NoDVD variant, 5 sections
        m.game = "minecraft-story-mode";
        m.season = 1;
        m.arch = Architecture::X86;
        return m;
    }
    if (mcsm1_shape && e.file_size == 12179904ull && e.fnv1a64 == 0x26B0BE1D74787BCDull) {
        m.id = "mcsm1_pc_x86_codex"; // CODEX NoDVD (crack-patched, same size)
        m.game = "minecraft-story-mode";
        m.season = 1;
        m.arch = Architecture::X86;
        return m;
    }
    if (mcsm1_shape) {
        m.id = "mcsm1_pc_x86_variant"; // same shape, unknown bytes: idle
        m.game = "minecraft-story-mode";
        m.season = 1;
        m.arch = Architecture::X86;
        return m;
    }
    // x64 PE: likely MCSM2-era, but unproven — never claim support.
    if (e.machine == 0x8664) {
        m.id = "unknown-x64";
        m.game = "unknown";
        m.arch = Architecture::X64;
        return m;
    }
    if (e.machine == 0x014C) {
        m.arch = Architecture::X86;
        return m;
    }
    return m;
}

ProfileStatus evaluate_support(const GameMatch& m) {
    // Exact-byte allowlist: only the measured known build is supported.
    // Everything else idles safely (Vanilla), including same-shape variants.
    if (std::string(m.id) == "mcsm1_pc_x86") return ProfileStatus::Supported;
    if (std::string(m.id) == "unknown-x64") return ProfileStatus::DefinedNotImplemented;
    if (std::string(m.id) != "unknown") return ProfileStatus::UnrecognizedBuild;
    return ProfileStatus::Unknown;
}

GameProfile init_from_exe(const std::string& exe_path, const std::string& log_path) {
    Logger log;
    GameProfile unknown;
    if (!log.open(log_path)) return unknown;
    log.info(std::string("TTMod framework v") + kVersion + " starting");
    ExeInfo e;
    auto parsed = parse_pe(exe_path);
    if (!parsed.ok()) {
        log.error(std::string("detect failed: ") + parsed.error().message);
        return unknown;
    }
    e = parsed.value();
    GameProfile prof = select_profile(e);
    log.info(std::string("Detected executable: ") + exe_path);
    char arch[64];
    snprintf(arch, sizeof arch, "Architecture: %s", arch_name(e.machine));
    log.info(arch);
    log.info(std::string("Game: ") + prof.game);
    char season[64];
    snprintf(season, sizeof season, "Season: %d", prof.season);
    log.info(season);
    log.info(std::string("Profile: ") + prof.id + " status=" + to_string(prof.status));
    RuntimeMode mode = mode_for_status(prof.status);
    log.info(std::string("Runtime mode: ") + to_string(mode));
    if (prof.status != ProfileStatus::Supported)
        log.warn("unsupported build: framework idle, game continues unmodified");
    else
        log.info("Framework initialization complete");
    return prof;
}

} // namespace ttmod

#include "ttmod/profile.hpp"
#include "ttmod/detect.hpp"
#include "ttmod/log.hpp"
#include "ttmod/runtime.hpp"
#include "ttmod/version.hpp"

namespace ttmod {

GameProfile select_profile(const ExeInfo& e) {
    GameProfile p;
    if (!e.ok) return p;
    // MCSM1 builds. Identity = size + timestamp + FNV-1a64 (size+timestamp
    // alone CONFUSES crack-patched variants, e.g. CODEX). SHA256 pinned in
    // docs/games/mcsm1.md. Only the byte-exact known build is "supported";
    // everything else idles safely (M9).
    const bool mcsm1_shape =
        e.machine == 0x014C && e.timestamp == 1463779093u && e.opt_magic == 0x10B;
    if (mcsm1_shape && e.file_size == 12179904ull && e.fnv1a64 == 0xA11CD391555291BEull) {
        p.id = "mcsm1_pc_x86";
        p.game = "minecraft-story-mode";
        p.season = 1;
        p.arch = Architecture::X86;
        p.status = ProfileStatus::Supported;
        return p;
    }
    if (mcsm1_shape && e.file_size == 11724800ull && e.fnv1a64 == 0xE507F1E444839F62ull) {
        p.id = "mcsm1_pc_x86_ali213"; // ALI213 NoDVD variant, 5 sections
        p.game = "minecraft-story-mode";
        p.season = 1;
        p.arch = Architecture::X86;
        p.status = ProfileStatus::UnrecognizedBuild;
        return p;
    }
    if (mcsm1_shape && e.file_size == 12179904ull && e.fnv1a64 == 0x26B0BE1D74787BCDull) {
        p.id = "mcsm1_pc_x86_codex"; // CODEX NoDVD (crack-patched, same size)
        p.game = "minecraft-story-mode";
        p.season = 1;
        p.arch = Architecture::X86;
        p.status = ProfileStatus::UnrecognizedBuild;
        return p;
    }
    if (mcsm1_shape) {
        p.id = "mcsm1_pc_x86_variant"; // same shape, unknown bytes: idle
        p.game = "minecraft-story-mode";
        p.season = 1;
        p.arch = Architecture::X86;
        p.status = ProfileStatus::UnrecognizedBuild;
        return p;
    }
    // x64 PE: likely MCSM2-era, but unproven — never claim support.
    if (e.machine == 0x8664) {
        p.id = "unknown-x64";
        p.game = "unknown";
        p.arch = Architecture::X64;
        p.status = ProfileStatus::DefinedNotImplemented;
        return p;
    }
    if (e.machine == 0x014C) {
        p.arch = Architecture::X86;
        return p;
    }
    return p;
}

GameProfile init_from_exe(const std::string& exe_path, const std::string& log_path) {
    Logger log;
    GameProfile unknown;
    if (!log.open(log_path)) return unknown;
    log.info(std::string("TTMod framework v") + kVersion + " starting");
    ExeInfo e = parse_pe(exe_path);
    if (!e.ok) {
        log.error(std::string("detect failed: ") + e.error);
        return unknown;
    }
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

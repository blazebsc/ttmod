#include "ttmod/runtime.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using ttmod::Capability;
    using ttmod::ProfileStatus;
    using ttmod::RuntimeMode;
    // Capability gates: the one known profile has everything, anything
    // else (null, unknown, empty) has nothing - fail-safe vanilla.
    assert(ttmod::profile_has("mcsm1_pc_x86", Capability::Menu));
    assert(ttmod::profile_has("mcsm1_pc_x86", Capability::LuaBridge));
    assert(ttmod::profile_has("mcsm1_pc_x86", Capability::FileOverrides));
    assert(!ttmod::profile_has(nullptr, Capability::Menu));
    assert(!ttmod::profile_has("", Capability::Plugins));
    assert(!ttmod::profile_has("mcsm2_pc_x64", Capability::Menu));
    // Mode mapping: supported -> active, everything else -> vanilla.
    assert(ttmod::mode_for_status(ProfileStatus::Supported) == RuntimeMode::Active);
    assert(ttmod::mode_for_status(ProfileStatus::Unknown) == RuntimeMode::Vanilla);
    assert(ttmod::mode_for_status(ProfileStatus::DefinedNotImplemented) == RuntimeMode::Vanilla);
    assert(ttmod::mode_for_status(ProfileStatus::UnrecognizedBuild) == RuntimeMode::Vanilla);
    assert(std::string(ttmod::to_string(RuntimeMode::Active)) == "active");
    assert(std::string(ttmod::to_string(ProfileStatus::Supported)) == "supported");
    assert(std::string(ttmod::to_string(ttmod::Architecture::X86)) == "x86");
    printf("runtime: capabilities OK\n");
    return 0;
}

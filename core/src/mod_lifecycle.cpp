#include "ttmod/mod_lifecycle.hpp"

namespace ttmod {

const char* to_string(ModRunState s) {
    switch (s) {
    case ModRunState::Prepared:
        return "prepared";
    case ModRunState::Running:
        return "running";
    case ModRunState::Stopping:
        return "stopping";
    case ModRunState::Unloaded:
        return "unloaded";
    default:
        return "failed";
    }
}

const char* to_string(ModStage s) {
    switch (s) {
    case ModStage::Entrypoint:
        return "entrypoint";
    case ModStage::Backend:
        return "backend";
    case ModStage::Compile:
        return "compile";
    case ModStage::Teardown:
        return "teardown";
    default:
        return "init";
    }
}

std::string Failure::describe() const {
    std::string out = to_string(stage);
    out += ": ";
    if (!error.object.empty()) out += error.object + ": ";
    out += error.message;
    return out;
}

std::string ModRunStatus::last_error_line() const {
    if (state != ModRunState::Failed && errors == 0) return "";
    std::string out;
    if (!failure.error.object.empty()) out += failure.error.object + ": ";
    out += failure.error.message.empty() ? "script error" : failure.error.message;
    if (errors > 1) out += " (+" + std::to_string(errors - 1) + " more)";
    return out;
}

ModRunState advance(ModRunState from, const char* event) {
    if (!event) return from;
    if (from == ModRunState::Failed || from == ModRunState::Unloaded) return from; // terminal
    if (std::string(event) == "fail") return ModRunState::Failed;
    switch (from) {
    case ModRunState::Prepared:
        if (std::string(event) == "ok") return ModRunState::Running;
        break;
    case ModRunState::Running:
        if (std::string(event) == "stop") return ModRunState::Stopping;
        break;
    case ModRunState::Stopping:
        if (std::string(event) == "unloaded") return ModRunState::Unloaded;
        break;
    default:
        break;
    }
    return from;
}

std::vector<SelectedEntrypoint> collect_script_entrypoints(const std::vector<ScriptModEntry>& mods,
                                                           Runtime runtime) {
    std::vector<SelectedEntrypoint> out;
    for (auto& m : mods) {
        if (!m.enabled) continue;
        if (m.runtime != runtime) continue;
        if (m.entrypoint.empty()) continue;
        out.push_back({m.id, m.entrypoint});
    }
    return out;
}

std::vector<TelltaleEntrypoint> collect_telltale_entrypoints(const std::vector<TelltaleMod>& mods) {
    std::vector<ScriptModEntry> generic;
    for (auto& m : mods)
        generic.push_back({m.id, m.telltale_runtime ? Runtime::TelltaleLua : Runtime::Lua, m.entrypoint, m.enabled});
    std::vector<TelltaleEntrypoint> out;
    for (auto& s : collect_script_entrypoints(generic, Runtime::TelltaleLua)) out.push_back({s.id, s.entrypoint});
    return out;
}

std::vector<ModRunStatus> run_mod_entrypoints(ScriptVm& vm, const std::vector<ModScript>& mods) {
    std::vector<ModRunStatus> out;
    // Backend match by what the VM actually is, not a hardcoded runtime:
    // a Lua mod needs a Lua VM, a Luau mod a Luau VM. Anything else -
    // including TelltaleLua, which runs on the game's own states, never
    // here - is a Backend failure on THIS VM.
    const std::string backend = vm.backend() ? vm.backend() : "";
    for (auto& m : mods) {
        ModRunStatus st;
        st.id = m.id;
        st.state = ModRunState::Prepared;
        auto fail = [&](ModStage stage, Error e) {
            st.failure = Failure{stage, std::move(e)};
            st.errors = 1;
            st.state = advance(st.state, "fail");
        };
        const bool runnable = (m.runtime == Runtime::Lua && backend == "lua") ||
                              (m.runtime == Runtime::Luau && backend == "luau");
        if (!runnable) {
            fail(ModStage::Backend, Error{"run-entrypoint", m.chunk_name, errcat::kType,
                                          std::string("no ") + to_string(m.runtime) + " backend on this VM"});
        } else if (m.source.empty()) {
            fail(ModStage::Entrypoint, Error{"run-entrypoint", m.chunk_name, errcat::kMissing, "empty chunk"});
        } else if (auto env = vm.create_env(m.id); !env.ok()) {
            fail(ModStage::Backend, env.error());
        } else if (auto r = vm.run_chunk(m.source, m.chunk_name, Value::string(m.id)); !r.ok()) {
            fail(ModStage::Init, r.error());
        } else {
            st.state = advance(st.state, "ok");
        }
        out.push_back(std::move(st));
    }
    return out;
}

} // namespace ttmod
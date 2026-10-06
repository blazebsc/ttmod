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

} // namespace ttmod
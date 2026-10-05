#pragma once
// One strict JSON implementation for all of core (Stage B): maintained
// parser (nlohmann, vendored) plus project policy - malformed input,
// trailing garbage, and duplicate object keys are all errors. Syntax
// parsing lives here; semantic validation lives with each schema.
#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <vector>

namespace ttmod {

using json = nlohmann::ordered_json;

// Parse strictly into out. False + error on malformed input, trailing
// garbage, oversize input, or any duplicate object key.
inline bool parse_json_value(const std::string& text, json& out, std::string& error) {
    if (text.size() > 1024 * 1024) {
        error = "input too large";
        return false;
    }
    std::vector<std::set<std::string>> stack;
    int depth = -1;
    bool dup = false;
    auto cb = [&](int, json::parse_event_t event, json& parsed) {
        if (dup) return false;
        if (event == json::parse_event_t::object_start) {
            stack.emplace_back();
            ++depth;
        } else if (event == json::parse_event_t::object_end) {
            stack.pop_back();
            --depth;
        } else if (event == json::parse_event_t::key) {
            if (depth < 0 || (size_t)depth >= stack.size()) {
                dup = true;
                return false;
            }
            if (!stack[(size_t)depth].insert(parsed.get<std::string>()).second) {
                dup = true;
                return false;
            }
        }
        return true;
    };
    try {
        out = json::parse(text, cb);
    } catch (const json::exception& e) {
        error = e.what();
        return false;
    }
    if (dup) {
        error = "duplicate object key";
        return false;
    }
    return true;
}

} // namespace ttmod

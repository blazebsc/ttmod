#pragma once
// One strict JSON implementation for all of core (Stage B): maintained
// parser (nlohmann, vendored) plus project policy - malformed input,
// trailing garbage, and duplicate object keys are all errors. Syntax
// parsing lives here; semantic validation lives with each schema.
#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <vector>
#include "ttmod/result.hpp"

namespace ttmod {

using json = nlohmann::ordered_json;

// Parse strictly. Failure carries operation "parse-json".
inline Result<json> parse_json_value(const std::string& text) {
    if (text.size() > 1024 * 1024)
        return Result<json>::fail(Error{"parse-json", "", errcat::kLimit, "input too large"});
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
    json out;
    try {
        out = json::parse(text, cb);
    } catch (const json::exception& e) {
        return Result<json>::fail(Error{"parse-json", "", errcat::kSyntax, e.what()});
    }
    if (dup)
        return Result<json>::fail(Error{"parse-json", "", errcat::kDuplicate, "duplicate object key"});
    return Result<json>::ok(std::move(out));
}

} // namespace ttmod

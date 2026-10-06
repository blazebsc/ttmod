// Validated mod ID. See modid.hpp.
#include "ttmod/modid.hpp"
#include "ttmod/validate.hpp"

namespace ttmod {

Result<ModId> ModId::parse(std::string_view id) {
    if (!is_valid_mod_id(id))
        return Result<ModId>::fail(Error{"parse-mod-id", std::string(id), errcat::kSyntax, "bad mod id"});
    return Result<ModId>::ok(ModId(std::string(id)));
}

} // namespace ttmod

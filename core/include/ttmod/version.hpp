#pragma once

namespace ttmod {
// Single source of framework version (M23 diagnostics, M17 API policy).
inline constexpr const char* kVersion = "0.12.0";
inline constexpr int kApiVersion = 5; // mirrors TTMOD_PLUGIN_API_VERSION
} // namespace ttmod

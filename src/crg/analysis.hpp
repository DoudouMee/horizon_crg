#pragma once
#include "nlohmann/json.hpp"
#include <string>

namespace horizon::crg {
using json = nlohmann::json;

// Self-contained snapshot of native block data with resolved entity pin definitions.
// All IDs are instance UUID paths; reference designators are presentation only.
json analyze(const json &snapshot, unsigned int max_cases = 256);
json load_snapshot(const std::string &blocks_filename, const std::string &pool_directory);
std::string to_dot(const json &report);
std::string scenarios_csv(const json &report);
const json &kind_catalog();
void decorate_pins(const json &metadata, json &pins);
void validate_metadata(const json &metadata, const json &pins);
} // namespace horizon::crg

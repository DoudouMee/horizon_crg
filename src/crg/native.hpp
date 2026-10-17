#pragma once
#include "nlohmann/json.hpp"
namespace horizon {
class BlocksSchematic;
class Component;
namespace crg {
nlohmann::json snapshot_from_native(const BlocksSchematic &blocks);
nlohmann::json pins_from_component(const Component &component);
} // namespace crg
} // namespace horizon

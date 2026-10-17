#include "native.hpp"
#include "analysis.hpp"
#include "blocks/blocks_schematic.hpp"
#include "pool/entity.hpp"

namespace horizon::crg {
json pins_from_component(const Component &component)
{
    json pins = json::object();
    for (const auto &[gid, gate] : component.entity->gates)
        for (const auto &[pid, pin] : gate.unit->pins)
            pins[(std::string)gid + "/" + (std::string)pid] = {
                    {"name", gate.name + "." + pin.primary_name},
                    {"direction",
                     Pin::direction_lut.lookup_reverse(component.get_effective_direction(UUIDPath<2>(gid, pid)))},
                    {"width", 1},
                    {"signal_type", "unknown"}};
    crg::decorate_pins(component.crg, pins);
    return pins;
}

json snapshot_from_native(const BlocksSchematic &blocks)
{
    json snapshot = {{"schema", "horizon.crg.snapshot"},
                     {"schema_version", 1},
                     {"top_block", (std::string)blocks.top_block},
                     {"blocks", json::object()}};
    for (const auto &[bid, item] : blocks.blocks) {
        auto b = item.block.serialize();
        for (const auto &[cid, comp] : item.block.components) {
            auto &c = b["components"][(std::string)cid];
            c["pin_definitions"] = pins_from_component(comp);
            c["locations"] = json::array();
            for (const auto &[sid, sheet] : item.schematic.sheets)
                for (const auto &[symid, sym] : sheet.symbols)
                    if (sym.component->uuid == cid)
                        c["locations"].push_back({{"sheet_uuid", (std::string)sid},
                                                  {"symbol_uuid", (std::string)symid},
                                                  {"placement", sym.placement.serialize()}});
        }
        snapshot["blocks"][(std::string)bid] = b;
    }
    const auto &meta = blocks.get_top_block_item().block.project_meta;
    if (meta.count("crg.modes"))
        snapshot["modes"] = json::parse(meta.at("crg.modes"));
    return snapshot;
}

} // namespace horizon::crg

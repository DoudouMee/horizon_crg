#include "analysis.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace horizon::crg {
namespace {
const json object = json::object();
const json array = json::array();
const json &field(const json &j, const char *key)
{
    return j.contains(key) ? j.at(key) : object;
}
std::string str(const json &j, const char *key)
{
    return j.at(key).get<std::string>();
}
json read(const std::filesystem::path &path)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Cannot read " + path.u8string());
    json j;
    in >> j;
    return j;
}
struct Signal {
    std::string type = "unknown";
    std::string state = "unknown";
    json frequency = nullptr;
    json level = nullptr;
    json path = array;
    json requirements = array;
};
json signal_json(const Signal &s)
{
    return {{"type", s.type},   {"state", s.state}, {"frequency_hz", s.frequency},
            {"level", s.level}, {"path", s.path},   {"requirements", s.requirements}};
}
struct Engine {
    json nodes = object, nets = object, instances = array, diagnostics = array;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> drivers;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> loads;
    std::map<std::string, std::string> parent;
    std::set<std::string> blocked;
    std::string root;

    void issue(const std::string &code, const std::string &id, const std::string &message,
               const std::string &severity = "error")
    {
        diagnostics.push_back({{"code", code}, {"target", id}, {"message", message}, {"severity", severity}});
        if (severity == "error")
            blocked.insert(id);
    }
    std::string find(const std::string &x)
    {
        if (!parent.count(x))
            parent[x] = x;
        if (parent.at(x) != x)
            parent[x] = find(parent.at(x));
        return parent.at(x);
    }
    void walk(const json &snapshot, const std::string &block_id, const std::string &path, const std::string &display,
              std::set<std::string> ancestors)
    {
        if (ancestors.count(block_id) || ancestors.size() >= 32)
            throw std::runtime_error("Recursive or excessive block hierarchy at " + path);
        ancestors.insert(block_id);
        const auto &b = snapshot.at("blocks").at(block_id);
        instances.push_back({{"id", path}, {"block_uuid", block_id}, {"name", display}});
        std::set<std::string> names;
        for (const auto &[uuid, n] : b.at("nets").items()) {
            auto id = path + "/" + uuid;
            nets[id] = {{"id", id}, {"aliases", array}, {"endpoints", array}};
            nets[id]["aliases"].push_back(
                    {{"instance", path}, {"block_uuid", block_id}, {"net_uuid", uuid}, {"name", n.value("name", "")}});
            find(id);
            auto name = n.value("name", "");
            if (!name.empty() && !names.insert(name).second)
                issue("duplicate_net_name", id, "Different nets share a name within one block: " + name);
        }
        names.clear();
        for (const auto &[uuid, c] : b.at("components").items()) {
            auto id = path + "/" + uuid;
            auto label = c.value("refdes", "");
            if (label.empty() || !names.insert(label).second)
                issue("duplicate_component_name", id, "Empty or repeated component reference: " + label);
            json node = {{"id", id},
                         {"name", display + "/" + label},
                         {"block_uuid", block_id},
                         {"component_uuid", uuid},
                         {"instance", path},
                         {"ports", object},
                         {"metadata", c.value("crg", object)},
                         {"locations", c.value("locations", array)}};
            const auto &pins = c.at("pin_definitions");
            try {
                validate_metadata(node.at("metadata"), pins);
            }
            catch (const std::exception &e) {
                issue("invalid_metadata", id, e.what());
            }
            if (c.value("nopopulate", false))
                issue("nopopulate", id, "PCB population flag is set; CRG semantics require explicit review", "warning");
            auto roles = node.at("metadata").value("ports", object);
            std::map<std::string, std::string> pin_roles;
            if (roles.is_object())
                for (const auto &[role, pin] : roles.items())
                    if (pin.is_string())
                        pin_roles[pin.get<std::string>()] = role;
            for (const auto &[pin, def] : pins.items()) {
                json net = nullptr;
                if (c.at("connections").contains(pin)) {
                    const auto &nu = c.at("connections").at(pin).at("net");
                    if (nu.is_string()) {
                        auto local = nu.get<std::string>();
                        if (!b.at("nets").contains(local))
                            issue("missing_net", id, "Pin references absent net: " + local);
                        else
                            net = path + "/" + local;
                    }
                }
                std::string role = pin_roles.count(pin) ? pin_roles.at(pin) : "";
                json port = {{"pin", pin},
                             {"name", def.value("name", "")},
                             {"direction", def.value("direction", "unknown")},
                             {"width", def.value("width", 1)},
                             {"signal_type", def.value("signal_type", "unknown")},
                             {"net", net},
                             {"role", role}};
                node["ports"][pin] = port;
                if (!net.is_null()) {
                    auto nid = net.get<std::string>();
                    nets[nid]["endpoints"].push_back({{"node", id},
                                                      {"pin", pin},
                                                      {"role", role},
                                                      {"direction", port.at("direction")},
                                                      {"width", port.at("width")},
                                                      {"signal_type", port.at("signal_type")}});
                }
                else
                    issue("unconnected_pin", id, "Unconnected pin: " + def.value("name", pin),
                          role.empty() ? "warning" : "error");
            }
            for (const auto &[pin, unused] : c.at("connections").items())
                if (!pins.contains(pin))
                    issue("missing_pin", id, "Connection references absent pin: " + pin);
            nodes[id] = node;
        }
        for (const auto &[uuid, inst] : field(b, "block_instances").items()) {
            auto child_id = str(inst, "block");
            auto child_path = path + "/" + uuid;
            if (!names.insert(inst.value("refdes", "")).second)
                issue("duplicate_component_name", child_path, "Repeated block instance reference");
            const auto &child = snapshot.at("blocks").at(child_id);
            walk(snapshot, child_id, child_path, display + "/" + inst.value("refdes", uuid), ancestors);
            const auto &connections = inst.at("connections");
            for (const auto &[port, n] : child.at("nets").items()) {
                if (!n.value("is_port", false))
                    continue;
                if (!connections.contains(port) || connections.at(port).at("net").is_null()) {
                    issue("unconnected_block_port", child_path + "/" + port, "Unconnected hierarchical port");
                    continue;
                }
                auto outer = connections.at(port).at("net").get<std::string>();
                if (!b.at("nets").contains(outer)) {
                    issue("missing_net", child_path, "Missing parent net");
                    continue;
                }
                parent[find(child_path + "/" + port)] = find(path + "/" + outer);
            }
            for (const auto &[port, unused] : connections.items())
                if (!child.at("nets").contains(port) || !child.at("nets").at(port).value("is_port", false))
                    issue("invalid_block_port", child_path, "Connection references a missing or non-port child net");
        }
    }
    void connect()
    {
        json canonical = object;
        for (const auto &[id, n] : nets.items()) {
            auto nid = find(id);
            if (!canonical.contains(nid))
                canonical[nid] = {{"id", nid}, {"aliases", array}, {"endpoints", array}};
            for (auto key : {"aliases", "endpoints"})
                for (const auto &x : n.at(key))
                    canonical[nid][key].push_back(x);
            if (blocked.count(id))
                blocked.insert(nid);
        }
        nets = canonical;
        for (auto &[id, node] : nodes.items())
            for (auto &[pin, port] : node["ports"].items()) {
                if (port.at("net").is_null())
                    continue;
                auto net = find(port.at("net").get<std::string>());
                port["net"] = net;
                auto direction = str(port, "direction");
                if (direction == "output")
                    drivers[net].emplace_back(id, pin);
                else if (direction == "input")
                    loads[net].emplace_back(id, pin);
                else
                    issue("unsupported_direction", id, "CRG pin needs an explicit input/output direction");
            }
        for (const auto &[id, n] : nets.items()) {
            if (drivers[id].size() > 1)
                issue("multiple_drivers", id, "Net has more than one driver");
            if (drivers[id].empty() && !loads[id].empty())
                issue("undriven_net", id, "Net has no modeled driver");
            std::set<int> widths;
            std::set<std::string> types;
            for (const auto &ep : n.at("endpoints")) {
                widths.insert(ep.at("width").get<int>());
                auto type = str(ep, "signal_type");
                if (type != "unknown")
                    types.insert(type);
            }
            if (widths.size() > 1)
                issue("width_mismatch", id, "Connected pins have different widths");
            if (types.size() > 1)
                issue("signal_type_mismatch", id, "Clock/reset/control pin types are incompatible");
        }
        // Conservative static cycle check includes every MUX branch and control dependency.
        std::map<std::string, int> color;
        std::vector<std::string> stack;
        std::function<void(const std::string &)> visit = [&](const std::string &id) {
            if (color[id] == 2)
                return;
            if (color[id] == 1) {
                auto begin = std::find(stack.begin(), stack.end(), id);
                for (auto it = begin; it != stack.end(); ++it)
                    issue("cycle", *it, "Combinational dependency cycle");
                return;
            }
            color[id] = 1;
            stack.push_back(id);
            for (const auto &[pin, p] : nodes.at(id).at("ports").items()) {
                if (p.at("direction") != "input" || p.at("net").is_null())
                    continue;
                for (const auto &d : drivers[p.at("net").get<std::string>()])
                    visit(d.first);
            }
            stack.pop_back();
            color[id] = 2;
        };
        for (const auto &[id, unused] : nodes.items())
            visit(id);
    }
    std::string role_net(const json &n, const std::string &role) const
    {
        for (const auto &[pin, p] : n.at("ports").items())
            if (p.at("role") == role && p.at("net").is_string())
                return p.at("net").get<std::string>();
        return "";
    }
    Signal evaluate(const std::string &id, const json &assignment, std::map<std::string, Signal> &cache,
                    std::set<std::string> &visiting)
    {
        if (cache.count(id))
            return cache.at(id);
        Signal s;
        if (id.empty() || blocked.count(id) || !nodes.contains(id) || !visiting.insert(id).second)
            return s;
        const auto &n = nodes.at(id);
        const auto &meta = n.at("metadata");
        const auto kind = meta.value("kind", "custom");
        const auto p = meta.value("parameters", object);
        auto input = [&](const std::string &role) {
            auto net = role_net(n, role);
            if (net.empty() || blocked.count(net) || drivers[net].size() != 1)
                return Signal{};
            return evaluate(drivers.at(net).front().first, assignment, cache, visiting);
        };
        auto control = [&]() {
            auto key = p.value("key", id);
            return assignment.contains(key) ? assignment.at(key) : json(nullptr);
        };
        if (kind == "clock_source") {
            s.type = "clock";
            if (p.contains("frequency_hz") && p.at("frequency_hz").is_number() && p.at("frequency_hz").get<double>() > 0
                && std::isfinite(p.at("frequency_hz").get<double>())) {
                s.frequency = p.at("frequency_hz");
                s.state = "running";
            }
        }
        else if (kind == "control_source" || kind == "reset_source") {
            s.type = kind == "reset_source" ? "reset" : "control";
            s.level = control();
            if (!s.level.is_null())
                s.state = "known";
        }
        else if (kind == "mux") {
            auto select = input("sel");
            if (select.level.is_number_integer()) {
                auto selected = std::to_string(select.level.get<int>());
                if (p.value("select_map", object).contains(selected)) {
                    s = input(p.at("select_map").at(selected).get<std::string>());
                    s.requirements.push_back(
                            "Apply and verify the declared clock-switch protocol before observing steady state");
                }
            }
            if (!p.contains("switch_protocol"))
                s.requirements.push_back("Missing MUX switch_protocol; transition test needs review");
        }
        else if (kind == "pll") {
            s = input("in");
            auto lock = input("lock");
            if (!p.contains("multiply") || !p.contains("divide") || !p.at("multiply").is_number()
                || !p.at("divide").is_number() || p.at("multiply").get<double>() <= 0
                || p.at("divide").get<double>() <= 0)
                s = Signal{};
            else if (lock.level.is_number_integer() && lock.level.get<int>() == 1) {
                if (s.frequency.is_number())
                    s.frequency =
                            s.frequency.get<double>() * p.at("multiply").get<double>() / p.at("divide").get<double>();
            }
            else {
                s.state = "unknown";
                s.frequency = nullptr;
                s.requirements.push_back("PLL lock is not established; output behavior is unspecified");
            }
        }
        else if (kind == "divider") {
            s = input("in");
            json divisor = p.value("divisor", json(nullptr));
            if (p.contains("divide_map")) {
                auto cfg = input("cfg");
                if (cfg.level.is_number_integer())
                    divisor = p.at("divide_map").value(std::to_string(cfg.level.get<int>()), json(nullptr));
                else
                    divisor = nullptr;
            }
            if (!divisor.is_number_integer() || divisor.get<int>() < 1)
                s = Signal{};
            else if (s.frequency.is_number())
                s.frequency = s.frequency.get<double>() / divisor.get<int>();
        }
        else if (kind == "gate") {
            s = input("in");
            auto en = input("en");
            auto test = input("test_en");
            bool known_on = (en.level.is_number_integer() && en.level.get<int>() == 1)
                            || (test.level.is_number_integer() && test.level.get<int>() == 1);
            bool known_off = en.level == 0 && (role_net(n, "test_en").empty() || test.level == 0);
            if (known_off) {
                s.type = "clock";
                s.state = "stopped";
                s.frequency = 0;
            }
            else if (!known_on) {
                s.state = "unknown";
                s.frequency = nullptr;
            }
        }
        else if (kind == "clock_output" || kind == "reset_output")
            s = input("in");
        else if (kind == "logic" || kind == "reset_combine") {
            auto a = input("in0"), b = input("in1");
            auto op = p.value("operation", "");
            s.type = kind == "logic" ? "control" : "reset";
            if (a.level.is_number_integer() && (op == "not" || b.level.is_number_integer())) {
                int x = a.level.get<int>(), y = b.level.is_number_integer() ? b.level.get<int>() : 0;
                if (op == "not")
                    s.level = x ? 0 : 1;
                else if (op == "and")
                    s.level = (x && y) ? 1 : 0;
                else if (op == "or")
                    s.level = (x || y) ? 1 : 0;
                else if (op == "xor")
                    s.level = (bool(x) != bool(y)) ? 1 : 0;
                if (!s.level.is_null())
                    s.state = "known";
                s.requirements = a.requirements;
                for (const auto &requirement : b.requirements)
                    s.requirements.push_back(requirement);
                s.path = a.path;
                for (const auto &v : b.path)
                    s.path.push_back(v);
            }
        }
        else if (kind == "reset_sync") {
            auto reset = input("in"), clk = input("clk");
            s = reset;
            s.type = "reset";
            if (!p.contains("input_active_level") || !p.contains("output_active_level") || !p.contains("stages")
                || !p.contains("async_assert") || !p.at("stages").is_number_integer() || p.at("stages").get<int>() < 2)
                s = Signal{};
            else if (reset.level.is_number_integer()) {
                auto active = p.at("output_active_level").get<int>();
                if (reset.level == p.at("input_active_level")) {
                    if (p.at("async_assert") == true) {
                        s.level = active;
                        s.state = "known";
                    }
                    else {
                        s.level = nullptr;
                        s.state = "unknown";
                    }
                }
                else if (clk.state == "running") {
                    for (const auto &requirement : clk.requirements)
                        s.requirements.push_back(requirement);
                    s.level = 1 - active;
                    s.state = "known";
                    s.requirements.push_back("Start from asserted reset; release only after "
                                             + std::to_string(p.at("stages").get<int>()) + " active clock edges at "
                                             + role_net(n, "clk"));
                }
                else {
                    s.level = nullptr;
                    s.state = "unknown";
                    s.requirements.push_back(
                            "Reset release cannot be predicted without a running synchronization clock");
                }
            }
        }
        if (kind == "custom")
            s.requirements.push_back("Custom primitive semantics need an analysis plugin");
        s.path.push_back(id);
        visiting.erase(id);
        cache[id] = s;
        return s;
    }
};
} // namespace

const json &kind_catalog()
{
    static const json catalog = {
            {"clock_source", {{"out", "output:clock"}}},
            {"control_source", {{"out", "output:control"}}},
            {"reset_source", {{"out", "output:reset"}}},
            {"pll", {{"in", "input:clock"}, {"lock", "input:control"}, {"out", "output:clock"}}},
            {"divider", {{"in", "input:clock"}, {"out", "output:clock"}}},
            {"mux",
             {{"in0", "input:clock"}, {"in1", "input:clock"}, {"sel", "input:control"}, {"out", "output:clock"}}},
            {"gate", {{"in", "input:clock"}, {"en", "input:control"}, {"out", "output:clock"}}},
            {"clock_output", {{"in", "input:clock"}}},
            {"logic", {{"in0", "input:control"}, {"out", "output:control"}}},
            {"reset_combine", {{"in0", "input:reset"}, {"out", "output:reset"}}},
            {"reset_sync", {{"in", "input:reset"}, {"clk", "input:clock"}, {"out", "output:reset"}}},
            {"reset_output", {{"in", "input:reset"}}},
            {"custom", object}};
    return catalog;
}
void decorate_pins(const json &metadata, json &pins)
{
    if (!metadata.is_object())
        return;
    auto kind = metadata.value("kind", "custom");
    for (const auto &[role, path] : field(metadata, "ports").items()) {
        if (!path.is_string() || !pins.contains(path.get<std::string>()))
            continue;
        std::string type = "unknown";
        if (kind_catalog().contains(kind) && kind_catalog().at(kind).contains(role)) {
            auto spec = kind_catalog().at(kind).at(role).get<std::string>();
            type = spec.substr(spec.find(':') + 1);
        }
        if (role == "test_en" || role == "cfg")
            type = "control";
        if (role == "in1" && (kind == "logic" || kind == "reset_combine"))
            type = kind == "logic" ? "control" : "reset";
        pins[path.get<std::string>()]["signal_type"] = type;
    }
    for (const auto &[pin, attrs] : field(metadata, "pin_attributes").items()) {
        if (!pins.contains(pin) || !attrs.is_object())
            continue;
        if (attrs.contains("width"))
            pins[pin]["width"] = attrs.at("width");
        if (attrs.contains("signal_type"))
            pins[pin]["signal_type"] = attrs.at("signal_type");
    }
}
void validate_metadata(const json &metadata, const json &pins)
{
    if (!metadata.is_object())
        throw std::runtime_error("CRG metadata must be an object");
    if (metadata.empty())
        throw std::runtime_error("Component has no CRG metadata");
    auto kind = str(metadata, "kind");
    if (!kind_catalog().contains(kind))
        throw std::runtime_error("Unknown primitive kind: " + kind);
    const auto &roles = metadata.at("ports");
    if (!roles.is_object() || !metadata.value("parameters", object).is_object())
        throw std::runtime_error("ports and parameters must be objects");
    std::set<std::string> mapped;
    for (const auto &[role, path] : roles.items()) {
        if (!path.is_string() || !pins.contains(path.get<std::string>()))
            throw std::runtime_error("Role " + role + " references an absent gate/pin UUID");
        if (!mapped.insert(path.get<std::string>()).second)
            throw std::runtime_error("Two roles map to the same pin");
    }
    auto expected = kind_catalog().at(kind);
    auto p = metadata.value("parameters", object);
    if ((kind == "logic" || kind == "reset_combine") && p.value("operation", "") != "not")
        expected["in1"] = "input:" + std::string(kind == "logic" ? "control" : "reset");
    if (kind == "divider" && p.contains("divide_map"))
        expected["cfg"] = "input:control";
    if (roles.contains("test_en"))
        expected["test_en"] = "input:control";
    auto positive = [&](const char *key, bool integer) {
        if (!p.contains(key) || !p.at(key).is_number() || (integer && !p.at(key).is_number_integer())
            || !std::isfinite(p.at(key).get<double>()) || p.at(key).get<double>() <= 0)
            throw std::runtime_error(std::string("Positive ") + (integer ? "integer " : "") + key + " is required");
    };
    if (kind == "clock_source")
        positive("frequency_hz", false);
    if (kind == "pll") {
        positive("multiply", false);
        positive("divide", false);
    }
    if (kind == "divider") {
        if (!p.contains("divide_map"))
            positive("divisor", true);
        else {
            if (!p.at("divide_map").is_object() || p.at("divide_map").empty())
                throw std::runtime_error("Nonempty divide_map is required");
            for (const auto &[key, value] : p.at("divide_map").items())
                if (!value.is_number_integer() || value.get<int>() < 1)
                    throw std::runtime_error("Divider values must be positive integers");
        }
    }
    if (kind == "mux") {
        if (!p.contains("select_map") || !p.at("select_map").is_object() || p.at("select_map").empty())
            throw std::runtime_error("MUX select_map is required");
        for (const auto &[key, value] : p.at("select_map").items())
            if (!value.is_string() || !roles.contains(value.get<std::string>()) || value == "sel" || value == "out")
                throw std::runtime_error("MUX mapping must reference a clock input role");
    }
    if (kind == "logic" || kind == "reset_combine") {
        auto op = p.value("operation", "");
        if (op != "not" && op != "and" && op != "or" && op != "xor")
            throw std::runtime_error("operation must be and/or/xor/not");
    }
    if (kind == "reset_sync") {
        positive("stages", true);
        if (p.at("stages").get<int>() < 2 || !p.contains("async_assert") || !p.at("async_assert").is_boolean())
            throw std::runtime_error("Reset synchronizer requires stages >= 2 and async_assert");
        for (const auto *key : {"input_active_level", "output_active_level"})
            if (!p.contains(key) || !p.at(key).is_number_integer() || (p.at(key) != 0 && p.at(key) != 1))
                throw std::runtime_error("Explicit reset polarity must be 0 or 1");
    }
    for (const auto &[pin, definition] : pins.items())
        if (!definition.value("width", json(1)).is_number_integer() || definition.value("width", 1) < 1)
            throw std::runtime_error("Pin width must be a positive integer");
    for (const auto &[role, type] : expected.items()) {
        if (!roles.contains(role))
            throw std::runtime_error("Missing semantic role: " + role);
        auto pin = pins.at(roles.at(role).get<std::string>());
        auto spec = type.get<std::string>();
        if (pin.at("direction") != spec.substr(0, spec.find(':')))
            throw std::runtime_error("Wrong pin direction for " + role);
    }
}

json analyze(const json &snapshot, unsigned int max_cases)
{
    if (snapshot.value("schema", "") != "horizon.crg.snapshot" || snapshot.at("schema_version") != 1)
        throw std::runtime_error("Unsupported CRG snapshot schema/version");
    if (max_cases < 1 || max_cases > 4096)
        throw std::runtime_error("max_cases must be 1..4096");
    Engine e;
    e.root = str(snapshot, "top_block");
    e.walk(snapshot, e.root, e.root, "Top", {});
    e.connect();
    json domains = object, bindings = object;
    for (const auto &[id, n] : e.nodes.items()) {
        if (e.blocked.count(id))
            continue;
        auto m = n.at("metadata");
        auto k = m.value("kind", "custom");
        auto p = m.value("parameters", object);
        if (k != "control_source" && k != "reset_source")
            continue;
        auto key = p.value("key", id);
        auto values = p.value("values", array);
        if (!values.is_array() || values.empty()
            || std::any_of(values.begin(), values.end(), [](const auto &v) { return !v.is_number_integer(); })) {
            e.issue("control_domain_missing", id, "Explicit integer values are required for control/reset sources");
            continue;
        }
        if (domains.contains(key)) {
            e.issue("duplicate_control_key", id, "Control key must be globally unique: " + key);
            continue;
        }
        domains[key] = values;
        bindings[key] = {{"node", id}, {"binding", p.value("binding", json(nullptr))}};
        if (!p.contains("binding"))
            e.issue("binding_missing", id, "Stimulus binding is unspecified", "warning");
    }
    auto modes = snapshot.value("modes", json{{"unconstrained", object}});
    if (!modes.is_object() || modes.empty())
        throw std::runtime_error("modes must be a nonempty object");
    json cases = array;
    bool truncated = false;
    for (const auto &mode_item : modes.items()) {
        const std::string mode = mode_item.key();
        const json fixed = mode_item.value();
        if (!fixed.is_object())
            throw std::runtime_error("Mode assignments must be objects");
        bool valid = true;
        for (const auto &[key, value] : fixed.items()) {
            if (!domains.contains(key)
                || std::find(domains.at(key).begin(), domains.at(key).end(), value) == domains.at(key).end()) {
                e.issue("invalid_mode", mode, "Mode references an unknown control or value: " + key);
                valid = false;
            }
        }
        if (!valid)
            continue;
        std::vector<std::string> keys;
        for (const auto &[key, unused] : domains.items())
            keys.push_back(key);
        json assignment = fixed;
        std::function<void(size_t)> enumerate = [&](size_t index) {
            if (cases.size() >= max_cases) {
                truncated = true;
                return;
            }
            if (index == keys.size()) {
                json outputs = array, steps = array;
                std::map<std::string, Signal> cache;
                std::set<std::string> visiting;
                bool complete = true;
                for (const auto &[key, val] : assignment.items()) {
                    steps.push_back({{"action", "set_control"},
                                     {"key", key},
                                     {"value", val},
                                     {"binding", bindings.at(key).at("binding")}});
                    if (bindings.at(key).at("binding").is_null())
                        complete = false;
                }
                for (const auto &[id, n] : e.nodes.items()) {
                    auto kind = n.at("metadata").value("kind", "");
                    if (kind != "clock_output" && kind != "reset_output")
                        continue;
                    auto value = signal_json(e.evaluate(id, assignment, cache, visiting));
                    value["node"] = id;
                    value["name"] = n.at("name");
                    value["observation_binding"] =
                            n.at("metadata").value("parameters", object).value("binding", json(nullptr));
                    if (value.at("state") == "unknown" || value.at("observation_binding").is_null())
                        complete = false;
                    for (const auto &requirement : value.at("requirements"))
                        if (requirement.get<std::string>().find("Missing ") == 0)
                            complete = false;
                    outputs.push_back(value);
                }
                if (outputs.empty())
                    complete = false;
                cases.push_back(
                        {{"id", "case_" + std::to_string(cases.size() + 1)},
                         {"mode", mode},
                         {"controls", assignment},
                         {"steps", steps},
                         {"observations", outputs},
                         {"status", complete ? "steady_state_candidate" : "needs_information"},
                         {"preconditions",
                          {"No transition ordering is inferred; establish the declared safe setup protocol first",
                           "Model expectations require review against the DUT specification"}}});
                return;
            }
            const auto &key = keys.at(index);
            if (fixed.contains(key)) {
                enumerate(index + 1);
                return;
            }
            for (const auto &value : domains.at(key)) {
                assignment[key] = value;
                enumerate(index + 1);
                if (truncated)
                    return;
            }
        };
        enumerate(0);
        if (truncated)
            break;
    }
    if (truncated)
        e.issue("case_limit", "", "Scenario enumeration reached its limit; coverage is incomplete", "warning");
    bool errors = std::any_of(e.diagnostics.begin(), e.diagnostics.end(),
                              [](const auto &d) { return d.at("severity") == "error"; });
    // Any structural error invalidates all automatic expectations, even disconnected faults.
    if (errors)
        for (auto &c : cases)
            c["status"] = "invalid_structure";
    return {{"schema", "horizon.crg.verification"},
            {"schema_version", 1},
            {"instances", e.instances},
            {"nodes", e.nodes},
            {"nets", e.nets},
            {"diagnostics", e.diagnostics},
            {"controls", bindings},
            {"scenarios", cases},
            {"coverage",
             {{"enumeration_complete", !truncated}, {"case_limit", max_cases}, {"transition_tests_generated", false}}},
            {"valid_structure", !errors},
            {"snapshot", snapshot}};
}

json load_snapshot(const std::string &blocks_filename, const std::string &pool_directory)
{
    namespace fs = std::filesystem;
    auto filename = fs::u8path(blocks_filename), pool = fs::u8path(pool_directory);
    auto index = read(filename);
    json objects = object;
    for (const auto &entry : fs::recursive_directory_iterator(pool)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        auto j = read(entry.path());
        if (j.contains("uuid") && (j.value("type", "") == "unit" || j.value("type", "") == "entity"))
            objects[str(j, "uuid")] = j;
    }
    json snapshot = {{"schema", "horizon.crg.snapshot"},
                     {"schema_version", 1},
                     {"top_block", index.at("top_block")},
                     {"blocks", object}};
    for (const auto &[uuid, info] : index.at("blocks").items()) {
        auto b = read(filename.parent_path() / fs::u8path(str(info, "block_filename")));
        if (b.at("uuid") != uuid)
            throw std::runtime_error("Block UUID does not match blocks.json");
        auto sch = read(filename.parent_path() / fs::u8path(str(info, "schematic_filename")));
        for (auto &[cid, c] : b["components"].items()) {
            c["pin_definitions"] = object;
            c["locations"] = array;
            const auto &entity = objects.at(str(c, "entity"));
            for (const auto &[gid, g] : entity.at("gates").items()) {
                const auto &unit = objects.at(str(g, "unit"));
                for (const auto &[pid, pin] : unit.at("pins").items()) {
                    auto def = json{{"name", pin.at("primary_name")},
                                    {"direction", pin.at("direction")},
                                    {"width", 1},
                                    {"signal_type", "unknown"}};
                    c["pin_definitions"][gid + "/" + pid] = def;
                }
            }
            decorate_pins(c.value("crg", object), c["pin_definitions"]);
            for (const auto &[sid, sheet] : sch.at("sheets").items())
                for (const auto &[syid, sym] : sheet.at("symbols").items())
                    if (sym.at("component") == cid)
                        c["locations"].push_back(
                                {{"sheet_uuid", sid}, {"symbol_uuid", syid}, {"placement", sym.at("placement")}});
        }
        snapshot["blocks"][uuid] = b;
    }
    const auto &top = snapshot.at("blocks").at(snapshot.at("top_block").get<std::string>());
    if (top.value("project_meta", object).contains("crg.modes"))
        snapshot["modes"] = json::parse(top.at("project_meta").at("crg.modes").get<std::string>());
    return snapshot;
}
std::string to_dot(const json &report)
{
    std::ostringstream out;
    out << "digraph CRG {\n  rankdir=LR;\n";
    auto quote = [](const std::string &s) { return json(s).dump(); };
    for (const auto &[id, node] : report.at("nodes").items())
        out << "  " << quote(id) << " [shape=box,label="
            << quote(node.at("name").get<std::string>() + "\n" + node.at("metadata").value("kind", "unmodeled"))
            << "];\n";
    for (const auto &[id, net] : report.at("nets").items()) {
        out << "  " << quote(id) << " [shape=point];\n";
        for (const auto &ep : net.at("endpoints")) {
            auto node = str(ep, "node");
            if (ep.at("direction") == "output")
                out << "  " << quote(node) << " -> " << quote(id);
            else
                out << "  " << quote(id) << " -> " << quote(node);
            out << " [label=" << quote(ep.at("role").get<std::string>()) << "];\n";
        }
    }
    out << "}\n";
    return out.str();
}
std::string scenarios_csv(const json &report)
{
    auto quote = [](std::string s) {
        std::string o = "\"";
        for (char c : s) {
            if (c == '"')
                o += '"';
            o += c;
        }
        return o + "\"";
    };
    std::ostringstream out;
    out << "case_id,mode,status,controls,observation,expected,requirements\r\n";
    for (const auto &c : report.at("scenarios"))
        for (const auto &obs : c.at("observations"))
            out << quote(str(c, "id")) << ',' << quote(str(c, "mode")) << ',' << quote(str(c, "status")) << ','
                << quote(c.at("controls").dump()) << ',' << quote(str(obs, "name")) << ',' << quote(obs.dump()) << ','
                << quote(obs.at("requirements").dump()) << "\r\n";
    return out.str();
}
} // namespace horizon::crg

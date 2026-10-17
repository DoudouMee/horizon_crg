#include "analysis.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using horizon::crg::json;
using namespace horizon::crg;

namespace {
void check(bool ok, const std::string &message)
{
    if (!ok)
        throw std::runtime_error(message);
}
std::string component(const json &snapshot, const std::string &refdes)
{
    const auto &b = snapshot.at("blocks").at(snapshot.at("top_block").get<std::string>());
    for (const auto &[id, c] : b.at("components").items())
        if (c.at("refdes") == refdes)
            return id;
    throw std::runtime_error("No component " + refdes);
}
json &comp(json &snapshot, const std::string &refdes)
{
    auto id = component(snapshot, refdes);
    return snapshot["blocks"][snapshot.at("top_block").get<std::string>()]["components"][id];
}
bool diagnostic(const json &report, const std::string &code)
{
    for (const auto &d : report.at("diagnostics"))
        if (d.at("code") == code)
            return true;
    return false;
}
json observation(const json &c, const std::string &name)
{
    for (const auto &o : c.at("observations"))
        if (o.at("name").get<std::string>().find("/" + name) != std::string::npos)
            return o;
    throw std::runtime_error("No observation " + name);
}
} // namespace
int main(int argc, char **argv)
{
    try {
        check(argc == 3, "Usage: crg-tests blocks.json pool");
        const auto original = load_snapshot(argv[1], argv[2]);
        auto report = analyze(original);
        check(report.at("valid_structure") == true, "Demo structure must be valid");
        check(report.at("diagnostics").empty(), "Demo must have no diagnostics");
        check(report.at("scenarios").size() == 24, "Expected 24 functional/DFT control combinations");
        for (const auto &c : report.at("scenarios")) {
            auto clk = observation(c, "CORE_CLK"), rst = observation(c, "CORE_RST");
            const auto &ctl = c.at("controls");
            const bool enabled = ctl.at("clk_en") == 1 || ctl.at("scan_enable") == 1;
            const double base = ctl.at("test_mode") == 1 ? 50e6 : 480e6;
            const double expected = enabled ? base / (ctl.at("div_cfg") == 0 ? 2 : 4) : 0;
            check(std::abs(clk.at("frequency_hz").get<double>() - expected) < 1e-4, "Clock expectation mismatch");
            check(clk.at("state") == (enabled ? "running" : "stopped"), "Gate state mismatch");
            if (ctl.at("por_n") == 0)
                check(rst.at("level") == 0, "Async reset assertion must work with stopped clock");
            else if (enabled)
                check(rst.at("level") == 1 && !rst.at("requirements").empty(), "Release needs 2 active edges");
            else
                check(rst.at("level").is_null() && c.at("status") == "needs_information",
                      "Stopped-clock release must stay unknown");
            check(!clk.at("path").empty(), "Observation provenance must be retained");
        }
        check(report.at("coverage").at("enumeration_complete") == true, "Enumeration should be complete");
        check(analyze(original, 24).at("coverage").at("enumeration_complete") == true, "Exact limit is not truncation");
        auto limited = analyze(original, 2);
        check(diagnostic(limited, "case_limit") && limited.at("coverage").at("enumeration_complete") == false,
              "Case cap must report incomplete coverage");
        auto s = original;
        comp(s, "DIV")["crg"]["parameters"]["divide_map"]["0"] = 0;
        check(diagnostic(analyze(s), "invalid_metadata"), "Zero divisor must be rejected");
        s = original;
        auto &div = comp(s, "DIV");
        auto &icg = comp(s, "ICG");
        div["connections"][div["crg"]["ports"]["in"].get<std::string>()] =
                icg["connections"][icg["crg"]["ports"]["out"].get<std::string>()];
        check(diagnostic(analyze(s), "cycle"), "Static loop must be detected");
        s = original;
        auto &xtal = comp(s, "XTAL");
        auto &test = comp(s, "TCLK");
        xtal["connections"][xtal["crg"]["ports"]["out"].get<std::string>()] =
                test["connections"][test["crg"]["ports"]["out"].get<std::string>()];
        auto bad = analyze(s);
        check(diagnostic(bad, "multiple_drivers"), "Multiple driver fault must be detected");
        for (const auto &c : bad.at("scenarios"))
            check(c.at("status") == "invalid_structure", "Invalid structure cannot have ready cases");
        s = original;
        comp(s, "PLL")["ports"] = json::object();
        auto pin = comp(s, "PLL")["crg"]["ports"]["in"].get<std::string>();
        comp(s, "PLL")["pin_definitions"][pin]["width"] = 2;
        check(diagnostic(analyze(s), "width_mismatch"), "Width mismatch must be detected");
        s = original;
        comp(s, "PLL")["pin_definitions"][pin]["signal_type"] = "reset";
        check(diagnostic(analyze(s), "signal_type_mismatch"), "Signal type mismatch must be detected");
        s = original;
        comp(s, "LOCK")["crg"]["parameters"]["values"] = {0};
        auto unlocked = analyze(s);
        for (const auto &c : unlocked.at("scenarios"))
            if (c.at("mode") == "functional" && c.at("controls").at("clk_en") == 1)
                check(observation(c, "CORE_CLK").at("state") == "unknown",
                      "Unlocked PLL cannot yield a frequency expectation");
        s = original;
        comp(s, "DIV")["crg"]["ports"]["in"] = "absent/pin";
        check(diagnostic(analyze(s), "invalid_metadata"), "Bad native pin references must be rejected");
        s = original;
        comp(s, "MUX")["crg"]["parameters"].erase("switch_protocol");
        auto missing_contract = analyze(s);
        for (const auto &c : missing_contract.at("scenarios"))
            check(c.at("status") == "needs_information",
                  "Missing timing contract must not produce complete candidates");
        s = original;
        s["modes"]["bad"] = {{"invented_control", 0}};
        check(diagnostic(analyze(s), "invalid_mode"), "Unknown mode control must be rejected");
        s = original;
        auto &r = comp(s, "RSTSYNC");
        r["crg"]["parameters"].erase("input_active_level");
        check(diagnostic(analyze(s), "invalid_metadata"), "Missing reset polarity must be rejected");
        // Reuse a definition twice. Each output port aliases a different parent net.
        json leaf = {
                {"uuid", "leaf"},
                {"nets", {{"out", {{"name", "clk"}, {"is_port", true}}}}},
                {"components",
                 {{"source",
                   {{"refdes", "OSC"},
                    {"connections", {{"g/p", {{"net", "out"}}}}},
                    {"pin_definitions",
                     {{"g/p", {{"name", "out"}, {"direction", "output"}, {"signal_type", "clock"}, {"width", 1}}}}},
                    {"crg",
                     {{"kind", "clock_source"},
                      {"ports", {{"out", "g/p"}}},
                      {"parameters", {{"frequency_hz", 1e6}}}}}}}}},
                {"block_instances", json::object()}};
        json top = {{"uuid", "top"},
                    {"nets", {{"a", {{"name", "a"}}}, {"b", {{"name", "b"}}}}},
                    {"components", json::object()},
                    {"block_instances",
                     {{"u1", {{"refdes", "U1"}, {"block", "leaf"}, {"connections", {{"out", {{"net", "a"}}}}}}},
                      {"u2", {{"refdes", "U2"}, {"block", "leaf"}, {"connections", {{"out", {{"net", "b"}}}}}}}}}};
        json hierarchy = {{"schema", "horizon.crg.snapshot"},
                          {"schema_version", 1},
                          {"top_block", "top"},
                          {"blocks", {{"top", top}, {"leaf", leaf}}}};
        auto h = analyze(hierarchy);
        check(h.at("valid_structure") == true && h.at("nodes").size() == 2 && h.at("nets").size() == 2,
              "Repeated block instances must remain independent");
        check(h.at("nets").at("top/a").at("aliases").size() == 2, "Parent/child nets must be joined by port UUID");
        check(h.at("nodes").contains("top/u1/source") && h.at("nodes").contains("top/u2/source"),
              "Stable instance identities required");
        check(to_dot(report).find("digraph CRG") == 0 && scenarios_csv(report).find("case_id,mode") == 0,
              "Export formats missing");
        std::cout << "CRG analysis: 24 scenarios and 15 fault/coverage/hierarchy regressions passed\n";
        return 0;
    }
    catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

#include "analysis.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char **argv)
{
    try {
        if (argc != 4) {
            std::cerr << "Usage: horizon-crg BLOCKS_JSON POOL_DIRECTORY OUTPUT_PREFIX\n";
            return 2;
        }
        const auto report = horizon::crg::analyze(horizon::crg::load_snapshot(argv[1], argv[2]));
        const std::string prefix = argv[3];
        for (const auto &entry :
             std::vector<std::pair<std::string, std::string>>{{".json", report.dump(2) + "\n"},
                                                              {".dot", horizon::crg::to_dot(report)},
                                                              {".csv", horizon::crg::scenarios_csv(report)}}) {
            std::ofstream out(std::filesystem::u8path(prefix + entry.first), std::ios::binary);
            out.exceptions(std::ios::failbit | std::ios::badbit);
            out << entry.second;
            out.close();
        }
        std::cout << report.at("nodes").size() << " nodes, " << report.at("nets").size() << " nets, "
                  << report.at("scenarios").size() << " cases, " << report.at("diagnostics").size() << " diagnostics\n";
        return report.at("valid_structure").get<bool>() ? 0 : 1;
    }
    catch (const std::exception &e) {
        std::cerr << "CRG: " << e.what() << '\n';
        return 2;
    }
}

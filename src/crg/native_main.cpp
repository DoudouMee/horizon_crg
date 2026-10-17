#include "analysis.hpp"
#include "native.hpp"
#include "blocks/blocks_schematic.hpp"
#include "pool/pool.hpp"
#include "pool/pool_manager.hpp"
#include "pool-update/pool-update.hpp"
#include "logger/logger.hpp"
#include "util/util.hpp"
#include <giomm.h>
#include <filesystem>
#include <iostream>

int main(int argc, char **argv)
{
    try {
        if (argc != 4) {
            std::cerr << "Usage: horizon-crg-native BLOCKS_JSON POOL_DIRECTORY OUTPUT_DIRECTORY\n"
                         "Indexes the native pool, verifies a native load/expand/save/reopen round trip, and exports a "
                         "report.\n";
            return 2;
        }
        Gio::init();
        horizon::create_cache_and_config_dir();
        horizon::PoolManager::init();
        bool load_error = false;
        horizon::Logger::get().set_log_handler([&](const auto &item) {
            if (item.level == horizon::Logger::Level::CRITICAL) {
                load_error = true;
                std::cerr << item.message << ": " << item.detail << '\n';
            }
        });
        horizon::pool_update(argv[2], [&](auto status, auto filename, auto message) {
            if (status == horizon::PoolUpdateStatus::ERROR || status == horizon::PoolUpdateStatus::FILE_ERROR) {
                load_error = true;
                std::cerr << filename << ": " << message << '\n';
            }
        });
        horizon::Pool pool(argv[2]);
        auto blocks = horizon::BlocksSchematic::new_from_file(argv[1], pool);
        if (load_error)
            throw std::runtime_error("Native loading reported errors");
        const auto before = horizon::crg::snapshot_from_native(blocks);
        for (auto &[id, item] : blocks.blocks)
            item.schematic.expand(true);
        const auto after = horizon::crg::snapshot_from_native(blocks);
        if (before.at("blocks").size() != after.at("blocks").size())
            throw std::runtime_error("Native block loss after expansion");
        for (const auto &[bid, b] : before.at("blocks").items())
            for (const auto &[cid, c] : b.at("components").items()) {
                const auto &other = after.at("blocks").at(bid).at("components").at(cid);
                if (c.at("connections") != other.at("connections") || c.at("crg") != other.at("crg"))
                    throw std::runtime_error("Native expand changed CRG connectivity or metadata: " + cid);
            }
        namespace fs = std::filesystem;
        auto out = fs::u8path(argv[3]);
        fs::create_directories(out);
        for (const auto &[id, item] : blocks.blocks) {
            for (const auto &fn : {item.block_filename, item.schematic_filename, item.symbol_filename})
                if (!fn.empty())
                    fs::create_directories((out / fs::u8path(fn)).parent_path());
            horizon::save_json_to_file((out / fs::u8path(item.block_filename)).u8string(), item.block.serialize());
            horizon::save_json_to_file((out / fs::u8path(item.schematic_filename)).u8string(),
                                       item.schematic.serialize());
            if (!item.symbol_filename.empty())
                horizon::save_json_to_file((out / fs::u8path(item.symbol_filename)).u8string(),
                                           item.symbol.serialize());
        }
        const auto index = (out / "blocks.json").u8string();
        horizon::save_json_to_file(index, blocks.serialize());
        auto reopened = horizon::BlocksSchematic::new_from_file(index, pool);
        const auto report = horizon::crg::analyze(horizon::crg::snapshot_from_native(reopened));
        if (load_error || report.at("valid_structure") != true)
            throw std::runtime_error("Native round trip produced errors");
        horizon::save_json_to_file((out / "verification.json").u8string(), report);
        const auto direct = horizon::crg::analyze(horizon::crg::load_snapshot(argv[1], argv[2]));
        if (report.at("scenarios") != direct.at("scenarios"))
            throw std::runtime_error("Native and headless expectations differ");
        std::cout << "Native round trip passed: " << report.at("nodes").size() << " components, "
                  << report.at("scenarios").size() << " matching scenarios\n";
        return 0;
    }
    catch (const std::exception &e) {
        std::cerr << "CRG native: " << e.what() << '\n';
        return 1;
    }
}

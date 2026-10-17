#include "imp_schematic.hpp"
#include "crg/analysis.hpp"
#include "crg/native.hpp"
#include "pool/entity.hpp"
#include "util/util.hpp"
#include <gtkmm.h>

namespace horizon {
namespace {
void message(Gtk::Window &parent, const std::string &text)
{
    Gtk::MessageDialog dialog(parent, text, false, Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK, true);
    dialog.run();
}
Gtk::TextView *add_text_page(Gtk::Notebook &book, const std::string &title, const std::string &text, bool editable)
{
    auto view = Gtk::manage(new Gtk::TextView);
    view->set_monospace(true);
    view->set_editable(editable);
    view->set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    view->get_buffer()->set_text(text);
    auto scroll = Gtk::manage(new Gtk::ScrolledWindow);
    scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroll->add(*view);
    book.append_page(*scroll, title);
    return view;
}
} // namespace

void ImpSchematic::edit_crg_component()
{
    if (read_only || core_schematic.tool_is_active() || core_schematic.get_block_symbol_mode())
        return;
    std::set<UUID> components;
    for (const auto &ref : canvas->get_selection()) {
        if (ref.type == ObjectType::SCHEMATIC_SYMBOL)
            components.insert(core_schematic.get_sheet()->symbols.at(ref.uuid).component->uuid);
    }
    if (components.size() != 1) {
        message(*main_window, "Select exactly one schematic component, then open CRG component properties.");
        return;
    }
    auto &comp = core_schematic.get_current_block()->components.at(*components.begin());
    Gtk::Dialog dialog("CRG component: " + comp.refdes, *main_window, true);
    dialog.set_default_size(820, 580);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Apply", Gtk::RESPONSE_OK);
    auto box = dialog.get_content_area();
    auto label =
            Gtk::manage(new Gtk::Label("Map semantic roles to native gate/pin UUIDs. Properties apply to every "
                                       "instance of this block definition.\nParameters contain frequency, control "
                                       "domains, stimulus/observation bindings and timing contracts."));
    label->set_line_wrap(true);
    label->set_margin_bottom(10);
    box->pack_start(*label, false, false);
    auto kind = Gtk::manage(new Gtk::ComboBoxText);
    for (const auto &[name, unused] : crg::kind_catalog().items())
        kind->append(name, name);
    kind->set_active_id(comp.crg.value("kind", "custom"));
    box->pack_start(*kind, false, false);
    auto book = Gtk::manage(new Gtk::Notebook);
    auto metadata = comp.crg.empty()
                            ? json{{"kind", "custom"}, {"ports", json::object()}, {"parameters", json::object()}}
                            : comp.crg;
    auto roles = add_text_page(*book, "Port roles (JSON)", metadata.value("ports", json::object()).dump(2), true);
    auto parameters =
            add_text_page(*book, "Parameters (JSON)", metadata.value("parameters", json::object()).dump(2), true);
    add_text_page(*book, "Available native pins", crg::pins_from_component(comp).dump(2), false);
    add_text_page(*book, "Required roles by kind", crg::kind_catalog().dump(2), false);
    box->pack_start(*book);
    dialog.show_all();
    while (dialog.run() == Gtk::RESPONSE_OK) {
        try {
            metadata["kind"] = kind->get_active_id().raw();
            metadata["ports"] = json::parse(roles->get_buffer()->get_text().raw());
            metadata["parameters"] = json::parse(parameters->get_buffer()->get_text().raw());
            crg::validate_metadata(metadata, crg::pins_from_component(comp));
            comp.crg = metadata;
            core_schematic.rebuild("edit CRG component properties");
            core_schematic.set_needs_save();
            break;
        }
        catch (const std::exception &e) {
            message(dialog, e.what());
        }
    }
}

void ImpSchematic::show_crg_report()
{
    if (core_schematic.tool_is_active() || core_schematic.get_block_symbol_mode())
        return;
    try {
        const auto report = crg::analyze(crg::snapshot_from_native(core_schematic.get_blocks()));
        Gtk::Dialog dialog("CRG verification analysis", *main_window, true);
        dialog.set_default_size(1040, 720);
        dialog.add_button("Close", Gtk::RESPONSE_CLOSE);
        dialog.add_button("Export JSON / DOT / CSV...", Gtk::RESPONSE_APPLY);
        auto summary = Gtk::manage(new Gtk::Label(
                std::to_string(report.at("nodes").size()) + " components | " + std::to_string(report.at("nets").size())
                + " nets | " + std::to_string(report.at("scenarios").size()) + " scenarios\n"
                + (report.at("valid_structure").get<bool>() ? "Structure checks passed. "
                                                            : "Structural errors require correction. ")
                + "Scenarios describe steady-state candidates; transition coverage requires a timing contract."));
        summary->set_line_wrap(true);
        summary->set_margin_bottom(10);
        dialog.get_content_area()->pack_start(*summary, false, false);
        auto book = Gtk::manage(new Gtk::Notebook);
        add_text_page(*book, "Diagnostics", report.at("diagnostics").dump(2), false);
        add_text_page(*book, "Verification scenarios", report.at("scenarios").dump(2), false);
        add_text_page(*book, "Structure and provenance", report.at("nodes").dump(2), false);
        add_text_page(*book, "Coverage", report.at("coverage").dump(2), false);
        dialog.get_content_area()->pack_start(*book);
        dialog.show_all();
        while (dialog.run() == Gtk::RESPONSE_APPLY) {
            Gtk::FileChooserDialog chooser(dialog, "Export CRG verification data", Gtk::FILE_CHOOSER_ACTION_SAVE);
            chooser.add_button("Cancel", Gtk::RESPONSE_CANCEL);
            chooser.add_button("Export", Gtk::RESPONSE_OK);
            chooser.set_do_overwrite_confirmation(true);
            chooser.set_current_folder(project_dir);
            chooser.set_current_name("crg-verification.json");
            if (chooser.run() == Gtk::RESPONSE_OK) {
                auto filename = chooser.get_filename();
                auto prefix = filename;
                if (prefix.size() >= 5 && prefix.substr(prefix.size() - 5) == ".json")
                    prefix.resize(prefix.size() - 5);
                {
                    auto json_file = make_ofstream(filename);
                    json_file.exceptions(std::ios::failbit | std::ios::badbit);
                    json_file << report.dump(2);
                    json_file.close();
                }
                // Confirm replacement of companions as well as the selected JSON file.
                bool companions = false;
                if (Glib::file_test(prefix + ".dot", Glib::FILE_TEST_EXISTS)
                    || Glib::file_test(prefix + ".csv", Glib::FILE_TEST_EXISTS)) {
                    Gtk::MessageDialog overwrite(dialog, "Replace existing DOT and CSV companion files?", false,
                                                 Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO, true);
                    companions = overwrite.run() == Gtk::RESPONSE_YES;
                }
                else
                    companions = true;
                if (companions) {
                    auto dot = make_ofstream(prefix + ".dot");
                    dot.exceptions(std::ios::failbit | std::ios::badbit);
                    dot << crg::to_dot(report);
                    dot.close();
                    auto csv = make_ofstream(prefix + ".csv");
                    csv.exceptions(std::ios::failbit | std::ios::badbit);
                    csv << crg::scenarios_csv(report);
                    csv.close();
                }
                message(dialog, "CRG export saved: " + filename);
            }
        }
    }
    catch (const std::exception &e) {
        message(*main_window, std::string("CRG analysis failed: ") + e.what());
    }
}
} // namespace horizon

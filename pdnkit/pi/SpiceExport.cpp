// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "pi/SpiceExport.h"

#include <cstddef>
#include <format>
#include <fstream>
#include <sstream>

namespace pdnkit::pi {

namespace {

std::string pos_str(const IrMesh& mesh, int nid) {
    if (nid < 0 || nid >= static_cast<int>(mesh.nodes.size())) return "?";
    const auto& n = mesh.nodes[nid];
    return std::format("({:.3f}, {:.3f}) mm",
                        n.x * 1000.0, n.y * 1000.0);
}

}  // namespace

std::string export_spice(const IrMesh& mesh, const SpiceExportConfig& cfg) {
    std::ostringstream out;

    out << "* " << (cfg.title.empty() ? std::string("pdnkit IR-drop network")
                                       : cfg.title)
        << "\n";
    out << "* " << mesh.nodes.size() << " nodes, "
        << mesh.resistors.size() << " resistors\n";
    out << "*\n";

    // Resistors. Conductance to resistance is 1/G; guard against 0.
    for (std::size_t i = 0; i < mesh.resistors.size(); ++i) {
        const auto& r = mesh.resistors[i];
        const double g = r.conductance;
        const double r_ohm = (g > 0.0) ? 1.0 / g : 1.0e12;  // open-ish
        out << "R" << i << " N" << r.from_node << " N" << r.to_node
            << " " << std::format("{:.6e}", r_ohm);
        if (cfg.include_position_comments) {
            out << "  ; " << pos_str(mesh, r.from_node)
                << " -> " << pos_str(mesh, r.to_node);
        }
        out << "\n";
    }

    // Current injectors.
    if (!mesh.node_currents.empty()) {
        std::size_t idx = 0;
        for (const auto& [nid, cur] : mesh.node_currents) {
            out << "I" << idx++ << " 0 N" << nid
                << " DC " << std::format("{:.6e}", cur);
            if (cfg.include_position_comments) {
                out << "  ; " << pos_str(mesh, nid);
            }
            out << "\n";
        }
    } else if (!mesh.source_node_ids.empty()) {
        const double per_src = cfg.default_total_current /
            static_cast<double>(mesh.source_node_ids.size());
        for (std::size_t i = 0; i < mesh.source_node_ids.size(); ++i) {
            const int nid = mesh.source_node_ids[i];
            out << "I" << i << " 0 N" << nid
                << " DC " << std::format("{:.6e}", per_src);
            if (cfg.include_position_comments) {
                out << "  ; " << pos_str(mesh, nid);
            }
            out << "\n";
        }
    }

    // Sinks: 0V voltage source to ground, so simulators can read I.
    for (std::size_t i = 0; i < mesh.sink_node_ids.size(); ++i) {
        const int nid = mesh.sink_node_ids[i];
        out << "Vsink" << i << " N" << nid << " 0 DC 0";
        if (cfg.include_position_comments) {
            out << "  ; " << pos_str(mesh, nid);
        }
        out << "\n";
    }

    if (cfg.add_op_directive) {
        out << ".op\n";
    }
    out << ".end\n";
    return out.str();
}

bool write_spice_netlist(const IrMesh& mesh,
                         const std::filesystem::path& path,
                         const SpiceExportConfig& cfg) {
    std::ofstream f(path);
    if (!f) return false;
    f << export_spice(mesh, cfg);
    return f.good();
}

}  // namespace pdnkit::pi

// SPICE netlist export for a pdnkit IR-drop mesh.
//
// Dumps the resistor network so the PDN can be loaded into ngspice or
// LTspice and co-simulated with a user-provided IC load model, VRM
// circuit, or external decap bank. Useful for going beyond what pdnkit
// itself solves (e.g. running a transient with a piecewise-linear current
// source defined elsewhere).
//
// Format: SPICE-3 compatible.
//   R<i>      <node+> <node-> <ohms>
//   I<i>      0 N<id> DC <amps>           (current injection at a source)
//   Vsink<i>  N<id> 0 DC 0                (sink tied to ground; measures I)
//   .op
//   .end
//
// Internal nodes are named N<id> matching IrMesh::Node::id. Node 0 in
// SPICE is ground.

#pragma once

#include <filesystem>
#include <string>

#include "pi/IrMesher.h"

namespace pdnkit::pi {

struct SpiceExportConfig {
    // First-line comment in the netlist. SPICE requires the first line
    // to be a title; this is it.
    std::string title;

    // Append a .op directive (DC operating point) so loading into a
    // simulator runs a solve automatically. On by default.
    bool add_op_directive = true;

    // Inline mm coordinates as ";" comments next to each R and source.
    // Helps when reading the netlist by hand. On by default.
    bool include_position_comments = true;

    // Used when mesh.node_currents is empty and we fall back to
    // source_node_ids / sink_node_ids. Total current is split equally
    // across sources. Default 1 A.
    double default_total_current = 1.0;
};

// Serialize the mesh to a SPICE netlist string.
std::string export_spice(const IrMesh& mesh,
                         const SpiceExportConfig& cfg = {});

// Write to a file. Returns true on success. Caller checks fs errors.
bool write_spice_netlist(const IrMesh& mesh,
                         const std::filesystem::path& path,
                         const SpiceExportConfig& cfg = {});

}  // namespace pdnkit::pi

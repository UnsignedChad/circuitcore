// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Model order reduction for static IR-drop networks.
//
// A typical pdnkit IrMesh has 10^3 - 10^5 nodes and as many resistors.
// Solving for IR drop on the full mesh is fine, but if you want to
// hand the PDN to a system-level simulator (LTspice, ngspice, Cadence
// Allegro PI) you do not want a 50000-port SPICE netlist -- you want
// a small multi-port equivalent with the same impedance at the ports
// that matter (the VRM, the load IC pins, the bulk decap nodes).
//
// Schur-complement reduction:
//
//   G * v = i  ->  partition nodes into P (ports, kept) and I (internal,
//                  eliminated). Then
//
//      [ G_PP G_PI ] [ v_P ]   [ i_P ]
//      [ G_IP G_II ] [ v_I ] = [ i_I = 0 ]
//
//      v_I = -G_II^-1 G_IP v_P
//      => G_red = G_PP - G_PI * G_II^-1 * G_IP   (kept-port conductance)
//
// The reduced N_port x N_port matrix has the same port-to-port impedance
// as the original. Cheap once: one dense factorization of G_II (size
// N_internal x N_internal) plus a few matmuls. For sparse G_II the
// existing CHOLMOD path can be reused.
//
// Result is suitable for emit-as-SPICE: one resistor between every pair
// of kept ports (off-diagonals) and one to ground (diagonal sums).

#pragma once

#include <Eigen/Dense>
#include <vector>

#include "pi/IrMesher.h"

namespace pdnkit::pi {

struct ReducedNetwork {
    // The kept port nodes in the original mesh, in the same order as
    // the rows/cols of G_reduced.
    std::vector<int> port_node_ids;

    // Dense N_port x N_port reduced conductance matrix (S). v_P[i] for
    // i in this matrix corresponds to mesh.nodes[port_node_ids[i]].
    Eigen::MatrixXd G_reduced;
};

// Reduce the mesh to a network containing only the listed ports.
// Returns an empty result on solver failure or invalid inputs.
ReducedNetwork reduce_to_ports(const IrMesh& mesh,
                                const std::vector<int>& port_node_ids);

// Convenience: dump a ReducedNetwork as a SPICE-3 fragment. Emits one
// R per non-trivial off-diagonal and one R to ground per node (capturing
// any self-conductance to a virtual ground). Node naming uses
// circuitcore::sexpr-style sanitized labels: NPxx where xx is the port
// index in port_node_ids.
std::string export_reduced_spice(const ReducedNetwork& net,
                                  const std::string& title = "");

}  // namespace pdnkit::pi

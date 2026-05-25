#include "pi/Mor.h"

#include <Eigen/Sparse>
#include <Eigen/SparseLU>
#include <algorithm>
#include <format>
#include <sstream>
#include <unordered_set>

namespace pdnkit::pi {

ReducedNetwork reduce_to_ports(const IrMesh& mesh,
                                const std::vector<int>& port_node_ids) {
    ReducedNetwork out;
    if (mesh.nodes.empty() || port_node_ids.empty()) return out;

    const int N = static_cast<int>(mesh.nodes.size());
    const int P = static_cast<int>(port_node_ids.size());

    // Validate port indices.
    std::unordered_set<int> port_set;
    for (int id : port_node_ids) {
        if (id < 0 || id >= N) return out;
        port_set.insert(id);
    }
    if (static_cast<int>(port_set.size()) != P) return out;  // duplicate

    // Build the global G (sparse). Standard nodal conductance:
    //   G(i,i) += g, G(j,j) += g, G(i,j) -= g, G(j,i) -= g
    using SpMat = Eigen::SparseMatrix<double>;
    using Trip  = Eigen::Triplet<double>;
    std::vector<Trip> trips;
    trips.reserve(mesh.resistors.size() * 4);
    for (const auto& r : mesh.resistors) {
        if (r.from_node < 0 || r.from_node >= N) continue;
        if (r.to_node   < 0 || r.to_node   >= N) continue;
        const double g = r.conductance;
        trips.emplace_back(r.from_node, r.from_node, g);
        trips.emplace_back(r.to_node,   r.to_node,   g);
        trips.emplace_back(r.from_node, r.to_node,  -g);
        trips.emplace_back(r.to_node,   r.from_node, -g);
    }
    SpMat G(N, N);
    G.setFromTriplets(trips.begin(), trips.end());

    // Permutation: ports first (in given order), then internals.
    std::vector<int> perm;
    perm.reserve(N);
    std::vector<int> inv_perm(N, -1);
    for (int p : port_node_ids) {
        inv_perm[p] = static_cast<int>(perm.size());
        perm.push_back(p);
    }
    for (int i = 0; i < N; ++i) {
        if (inv_perm[i] < 0) {
            inv_perm[i] = static_cast<int>(perm.size());
            perm.push_back(i);
        }
    }
    const int I = N - P;
    if (I <= 0) {
        // All nodes are kept -- reduction is the identity (dense).
        Eigen::MatrixXd Gd = Eigen::MatrixXd(G);
        out.port_node_ids = port_node_ids;
        out.G_reduced = Eigen::MatrixXd(P, P);
        for (int i = 0; i < P; ++i)
            for (int j = 0; j < P; ++j)
                out.G_reduced(i, j) = Gd(port_node_ids[i], port_node_ids[j]);
        return out;
    }

    // Permute G into block form.
    Eigen::MatrixXd Gd = Eigen::MatrixXd(G);
    Eigen::MatrixXd Gp(N, N);
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            Gp(inv_perm[i], inv_perm[j]) = Gd(i, j);

    Eigen::MatrixXd G_PP = Gp.topLeftCorner(P, P);
    Eigen::MatrixXd G_PI = Gp.topRightCorner(P, I);
    Eigen::MatrixXd G_IP = Gp.bottomLeftCorner(I, P);
    Eigen::MatrixXd G_II = Gp.bottomRightCorner(I, I);

    // G_red = G_PP - G_PI * G_II^-1 * G_IP. Solve the linear system
    // rather than forming the inverse explicitly.
    Eigen::FullPivLU<Eigen::MatrixXd> lu(G_II);
    if (!lu.isInvertible()) return out;
    Eigen::MatrixXd X = lu.solve(G_IP);
    out.G_reduced = G_PP - G_PI * X;
    out.port_node_ids = port_node_ids;
    return out;
}

std::string export_reduced_spice(const ReducedNetwork& net,
                                  const std::string& title) {
    std::ostringstream out;
    out << "* " << (title.empty() ? std::string("pdnkit reduced PI network")
                                   : title)
        << "\n";
    const int P = static_cast<int>(net.port_node_ids.size());
    out << "* " << P << " ports\n*\n";
    // Off-diagonal resistors: R between port i and j of value -1/G(i,j).
    // (Off-diagonal of nodal conductance is the negative of the inter-node
    // conductance.)
    int rcount = 0;
    for (int i = 0; i < P; ++i) {
        for (int j = i + 1; j < P; ++j) {
            const double g_off = net.G_reduced(i, j);
            const double g_branch = -g_off;
            if (std::abs(g_branch) < 1.0e-15) continue;
            const double r_ohm = 1.0 / g_branch;
            out << "R" << rcount++ << " NP" << i << " NP" << j
                << " " << std::format("{:.6e}", r_ohm) << "\n";
        }
    }
    // Diagonal residual (self-conductance to ground): row_sum should be 0
    // for a pure resistor network; non-zero residual indicates a path to
    // ground (e.g. via sinks) and shows up as a resistor to node 0.
    for (int i = 0; i < P; ++i) {
        double row_sum = 0.0;
        for (int j = 0; j < P; ++j) row_sum += net.G_reduced(i, j);
        if (std::abs(row_sum) < 1.0e-15) continue;
        const double r_ground = 1.0 / row_sum;
        out << "R" << rcount++ << " NP" << i << " 0 "
            << std::format("{:.6e}", r_ground) << "\n";
    }
    out << ".end\n";
    return out.str();
}

}  // namespace pdnkit::pi

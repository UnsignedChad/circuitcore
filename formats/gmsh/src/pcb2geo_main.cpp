// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// pcb2geo -- minimal CLI for the experimental conformal-mesh-export
// branch. Reads a .kicad_pcb, runs the parser, hands the resulting
// Board to GeoWriter, writes a Gmsh .geo file.
//
// Intended workflow:
//
//   pcb2geo board.kicad_pcb -o board.geo
//   gmsh -3 board.geo -o board.msh        # tet-mesh it
//   ElmerGrid 14 2 board.msh              # convert to Elmer's format
//   ElmerSolver case.sif                  # run the FE solve
//
// CLI is deliberately bare: positional input, -o for output, --cl for
// the characteristic length. No CLI11 dep so this stays cheap to build.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "circuitcore/formats/gmsh/GeoWriter.h"
#include "circuitcore/formats/kicad/PcbParser.h"

namespace {

void usage(const char* prog) {
    std::fprintf(stderr,
        "usage: %s <board.kicad_pcb> -o <out.geo> [--cl <length_m>]\n"
        "       [--copper-thickness <m>] [--substrate-thickness <m>]\n"
        "       [--no-fragments]\n"
        "\n"
        "experimental: emits a Gmsh .geo script that gmsh -3 can\n"
        "tetrahedralise into a conformal mesh suitable for Elmer.\n",
        prog);
}

}  // namespace

int main(int argc, char** argv) {
    std::string in_path;
    std::string out_path;
    circuitcore::formats::gmsh::GeoWriteOptions opts;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        if (a == "-o" && i + 1 < argc) { out_path = argv[++i]; continue; }
        if (a == "--cl" && i + 1 < argc) {
            opts.characteristic_length_m = std::stod(argv[++i]); continue;
        }
        if (a == "--copper-thickness" && i + 1 < argc) {
            opts.copper_thickness_m = std::stod(argv[++i]); continue;
        }
        if (a == "--substrate-thickness" && i + 1 < argc) {
            opts.substrate_thickness_fallback_m = std::stod(argv[++i]);
            continue;
        }
        if (a == "--no-fragments") {
            opts.boolean_fragments = false; continue;
        }
        if (!a.empty() && a[0] != '-') {
            if (in_path.empty()) { in_path = a; continue; }
        }
        std::fprintf(stderr, "pcb2geo: unrecognised arg: %s\n", argv[i]);
        usage(argv[0]);
        return 2;
    }
    if (in_path.empty() || out_path.empty()) { usage(argv[0]); return 2; }

    auto board_or = circuitcore::formats::kicad::PcbParser::parse_file(in_path);
    if (!board_or) {
        std::fprintf(stderr, "pcb2geo: parse failed: %s\n",
                      board_or.error().format().c_str());
        return 1;
    }
    const auto& board = board_or.value();

    auto rc = circuitcore::formats::gmsh::write_board_geo(
        board, out_path, opts);
    if (!rc) {
        std::fprintf(stderr, "pcb2geo: write failed: %s\n",
                      rc.error().message.c_str());
        return 1;
    }
    std::fprintf(stderr,
        "pcb2geo: wrote %s (%zu zones, outline %zu segs)\n",
        out_path.c_str(),
        board.zones.size(), board.outline.size());
    return 0;
}

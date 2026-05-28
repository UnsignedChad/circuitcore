// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "circuitcore/formats/gmsh/GeoWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace circuitcore::formats::gmsh {

namespace {

// Sanitise a KiCad layer name ("F.Cu") into something safe for a Gmsh
// Physical group name (no dots, no slashes -- those are syntax in .geo).
std::string sanitise_layer_name(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '.' || c == '/' || c == ' ') out.push_back('_');
        else                                    out.push_back(c);
    }
    return out;
}

// Chain raw outline segments into closed polygons by endpoint matching.
// Edge.Cuts comes out of the parser as an unordered list of line
// segments (arcs already get tessellated into chains), so for a board
// with rounded corners we get a mix of "geometric neighbours" that
// happen to be adjacent in the file, plus stragglers from gr_lines.
// Walk the set, picking up segments whose start matches the running
// chain's end (or end matches start) until the chain closes. The
// largest closed ring is the outer board outline; smaller closed rings
// are interior cutouts (e.g. mounting holes); chains that don't close
// are dropped with a noisy warning rather than silently producing a
// broken .geo.
struct Ring {
    std::vector<board::Point2> pts;
    double area_signed = 0.0;
};

constexpr double kSnapTol = 1.0e-6;  // 1 µm: KiCad rounds to 0.001 mm

bool point_eq(const board::Point2& a, const board::Point2& b) {
    return std::abs(a.x - b.x) < kSnapTol && std::abs(a.y - b.y) < kSnapTol;
}

double signed_area(const std::vector<board::Point2>& ring) {
    double a = 0.0;
    const std::size_t n = ring.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto& p = ring[i];
        const auto& q = ring[(i + 1) % n];
        a += (p.x * q.y) - (q.x * p.y);
    }
    return 0.5 * a;
}

std::vector<Ring> chain_outline(const std::vector<board::OutlineSegment>& segs) {
    std::vector<Ring> rings;
    std::vector<bool> used(segs.size(), false);

    for (std::size_t seed = 0; seed < segs.size(); ++seed) {
        if (used[seed]) continue;
        Ring r;
        r.pts.push_back(segs[seed].start);
        r.pts.push_back(segs[seed].end);
        used[seed] = true;
        bool grew = true;
        while (grew) {
            grew = false;
            const auto tail = r.pts.back();
            for (std::size_t i = 0; i < segs.size(); ++i) {
                if (used[i]) continue;
                if (point_eq(segs[i].start, tail)) {
                    r.pts.push_back(segs[i].end);
                    used[i] = true; grew = true; break;
                }
                if (point_eq(segs[i].end, tail)) {
                    r.pts.push_back(segs[i].start);
                    used[i] = true; grew = true; break;
                }
            }
            if (point_eq(r.pts.back(), r.pts.front())) break;
        }
        if (r.pts.size() >= 4 && point_eq(r.pts.back(), r.pts.front())) {
            r.pts.pop_back();  // drop the duplicate closing vertex
            r.area_signed = signed_area(r.pts);
            rings.push_back(std::move(r));
        }
    }
    return rings;
}

// Stream helper -- bumps `next_id` and returns the assigned tag.
struct TagAllocator {
    int next = 1;
    int alloc() { return next++; }
};

// Emit a planar surface from a single closed ring, returning the
// Surface tag. Uses raw Point/Line/Curve Loop/Plane Surface because
// OpenCASCADE's polygon helper expects the input to be in CCW order
// and we'd rather feed it manually than gamble on orientation.
int emit_polygon_surface(std::ostream& out,
                          TagAllocator& tags,
                          const std::vector<board::Point2>& pts,
                          double z,
                          double cl) {
    const int p0 = tags.next;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const int pid = tags.alloc();
        out << "Point(" << pid << ") = {"
            << pts[i].x << ", " << pts[i].y << ", " << z
            << ", " << cl << "};\n";
    }
    const int l0 = tags.next;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const int lid  = tags.alloc();
        const int from = p0 + static_cast<int>(i);
        const int to   = p0 + static_cast<int>((i + 1) % pts.size());
        out << "Line(" << lid << ") = {" << from << ", " << to << "};\n";
    }
    const int loop = tags.alloc();
    out << "Curve Loop(" << loop << ") = {";
    for (std::size_t i = 0; i < pts.size(); ++i) {
        if (i) out << ", ";
        out << (l0 + static_cast<int>(i));
    }
    out << "};\n";
    const int surf = tags.alloc();
    out << "Plane Surface(" << surf << ") = {" << loop << "};\n";
    return surf;
}

// Emit a polygon with holes (a "filled" zone). The outline ring is one
// Curve Loop; each hole ring is another. Plane Surface takes a list of
// loops -- the first is the outer boundary, subsequent loops are
// subtracted as holes. Returns the surface tag.
int emit_polygon_with_holes(std::ostream& out,
                             TagAllocator& tags,
                             const board::Polygon& poly,
                             double z,
                             double cl) {
    // Outer ring
    const int outer_p0 = tags.next;
    for (const auto& p : poly.outline) {
        const int pid = tags.alloc();
        out << "Point(" << pid << ") = {"
            << p.x << ", " << p.y << ", " << z << ", " << cl << "};\n";
    }
    const int outer_l0 = tags.next;
    for (std::size_t i = 0; i < poly.outline.size(); ++i) {
        const int lid  = tags.alloc();
        const int from = outer_p0 + static_cast<int>(i);
        const int to   = outer_p0 + static_cast<int>(
                            (i + 1) % poly.outline.size());
        out << "Line(" << lid << ") = {" << from << ", " << to << "};\n";
    }
    const int outer_loop = tags.alloc();
    out << "Curve Loop(" << outer_loop << ") = {";
    for (std::size_t i = 0; i < poly.outline.size(); ++i) {
        if (i) out << ", ";
        out << (outer_l0 + static_cast<int>(i));
    }
    out << "};\n";

    std::vector<int> loops;
    loops.push_back(outer_loop);

    for (const auto& hole : poly.holes) {
        if (hole.size() < 3) continue;
        const int hp0 = tags.next;
        for (const auto& p : hole) {
            const int pid = tags.alloc();
            out << "Point(" << pid << ") = {"
                << p.x << ", " << p.y << ", " << z << ", " << cl << "};\n";
        }
        const int hl0 = tags.next;
        for (std::size_t i = 0; i < hole.size(); ++i) {
            const int lid  = tags.alloc();
            const int from = hp0 + static_cast<int>(i);
            const int to   = hp0 + static_cast<int>((i + 1) % hole.size());
            out << "Line(" << lid << ") = {" << from << ", " << to << "};\n";
        }
        const int hloop = tags.alloc();
        out << "Curve Loop(" << hloop << ") = {";
        for (std::size_t i = 0; i < hole.size(); ++i) {
            if (i) out << ", ";
            out << (hl0 + static_cast<int>(i));
        }
        out << "};\n";
        loops.push_back(hloop);
    }

    const int surf = tags.alloc();
    out << "Plane Surface(" << surf << ") = {";
    for (std::size_t i = 0; i < loops.size(); ++i) {
        if (i) out << ", ";
        out << loops[i];
    }
    out << "};\n";
    return surf;
}

}  // namespace

std::expected<void, GeoWriteError> write_board_geo(
    const board::Board& board,
    std::ostream& out,
    const GeoWriteOptions& opts) {
    if (!out) return std::unexpected(GeoWriteError{"output stream bad"});
    if (board.outline.empty()) {
        return std::unexpected(GeoWriteError{
            "board has no Edge.Cuts outline to extrude as substrate"});
    }

    const auto rings = chain_outline(board.outline);
    if (rings.empty()) {
        return std::unexpected(GeoWriteError{
            "Edge.Cuts segments did not form any closed polygon"});
    }
    // Outer boundary = ring with the largest absolute area.
    std::size_t outer_idx = 0;
    double      outer_a   = std::abs(rings[0].area_signed);
    for (std::size_t i = 1; i < rings.size(); ++i) {
        const double a = std::abs(rings[i].area_signed);
        if (a > outer_a) { outer_a = a; outer_idx = i; }
    }
    const auto& outer_ring = rings[outer_idx].pts;

    const double sub_thick = (board.stackup.total_thickness > 0.0)
        ? board.stackup.total_thickness
        : opts.substrate_thickness_fallback_m;

    TagAllocator tags;

    out << "// generated by circuitcore::formats::gmsh::write_board_geo\n"
        << "// units: metres. mesh with:  gmsh -3 <this>.geo -o <this>.msh\n\n"
        << "SetFactory(\"OpenCASCADE\");\n"
        << "Geometry.OCCTargetUnit = \"M\";\n"
        << "cl = " << opts.characteristic_length_m << ";\n\n";

    // -------- substrate --------------------------------------------------
    out << "// substrate (full board outline, " << sub_thick * 1e3
        << " mm thick FR-4)\n";
    const int sub_surf = emit_polygon_surface(out, tags, outer_ring,
                                                 0.0, opts.characteristic_length_m);
    const std::string sub_var = "vsub";
    out << sub_var << "[] = Extrude {0, 0, " << sub_thick
        << "} { Surface{" << sub_surf << "}; };\n\n";

    // Interior cutouts (mounting holes etc.): later. For v0 the
    // additional rings are ignored -- the resulting mesh has a solid
    // substrate where KiCad shows a hole, which over-estimates the
    // thermal mass slightly but doesn't break anything downstream.

    // -------- copper per layer ------------------------------------------
    // Build a per-layer list of {filled-poly index in board.zones} -> list
    // of [zone_index, filled_index] so we can iterate cleanly.
    std::unordered_map<int, std::vector<std::pair<int, int>>> per_layer;
    for (int zi = 0; zi < static_cast<int>(board.zones.size()); ++zi) {
        const auto& z = board.zones[zi];
        const auto* L = board.find_layer(z.layer_ordinal);
        if (!L || !L->is_copper()) continue;
        for (int fi = 0; fi < static_cast<int>(z.filled.size()); ++fi) {
            if (z.filled[fi].outline.size() < 3) continue;
            per_layer[z.layer_ordinal].emplace_back(zi, fi);
        }
    }

    // Each layer needs a Z plane. Without a stackup we naively distribute
    // copper layers evenly through the substrate -- F.Cu on top, B.Cu on
    // bottom, inner layers spread between. This is a placeholder until
    // the parser's per-layer thickness is fully respected.
    std::vector<int> copper_ordinals;
    for (const auto& L : board.stackup.layers) {
        if (L.is_copper()) copper_ordinals.push_back(L.ordinal);
    }
    // Sort by KiCad's render priority: 0 (F.Cu) is top, 31 (B.Cu) is
    // bottom, inner layers (1, 2, 3...) sit in descending z between.
    std::sort(copper_ordinals.begin(), copper_ordinals.end(),
               [](int a, int b) {
                   auto rank = [](int o) {
                       if (o == 0)  return 0;          // F.Cu -- top
                       if (o == 31) return 1000000;    // B.Cu -- bottom
                       return o;                        // inner layers
                   };
                   return rank(a) < rank(b);
               });
    std::unordered_map<int, double> ord_to_z;
    const int n_copper = static_cast<int>(copper_ordinals.size());
    if (n_copper >= 1) {
        for (int i = 0; i < n_copper; ++i) {
            // i=0 (F.Cu) goes at z = sub_thick (top), i=n-1 (B.Cu) at z = 0.
            const double t = (n_copper == 1) ? 1.0
                : 1.0 - static_cast<double>(i) / (n_copper - 1);
            ord_to_z[copper_ordinals[i]] = t * sub_thick;
        }
    }

    std::unordered_map<int, std::vector<std::string>> copper_vars_by_layer;
    int zone_seq = 0;
    for (const auto& [ord, list] : per_layer) {
        auto it = ord_to_z.find(ord);
        const double z = (it != ord_to_z.end()) ? it->second : 0.0;
        // Copper sits ON the substrate face (z above for top, z below
        // for bottom). For simplicity centre the copper *on* the z plane.
        const double z_lo = z - 0.5 * opts.copper_thickness_m;
        const auto* L = board.find_layer(ord);
        const std::string ln = L ? sanitise_layer_name(L->name)
                                  : ("ord" + std::to_string(ord));
        out << "// copper layer " << ln << " (" << list.size()
            << " filled polygons, z = " << z * 1e3 << " mm)\n";
        for (const auto& [zi, fi] : list) {
            const auto& poly = board.zones[zi].filled[fi];
            const int surf = emit_polygon_with_holes(
                out, tags, poly, z_lo, opts.characteristic_length_m);
            const std::string var = "vcu_" + ln + "_" + std::to_string(zone_seq++);
            out << var << "[] = Extrude {0, 0, "
                << opts.copper_thickness_m << "} { Surface{" << surf
                << "}; };\n";
            copper_vars_by_layer[ord].push_back(var);
        }
        out << "\n";
    }

    // -------- Boolean fragments -----------------------------------------
    // Cuts overlapping volumes into a single coherent topology -- without
    // this the mesher emits two tets at every spot where copper and
    // substrate share a face, and the material assignment downstream
    // gets ambiguous.
    if (opts.boolean_fragments) {
        out << "// fragment everything so material boundaries are honoured\n";
        out << "BooleanFragments{ Volume{" << sub_var << "[1]}; Delete; }{ ";
        bool first = true;
        for (const auto& [ord, vars] : copper_vars_by_layer) {
            for (const auto& v : vars) {
                if (!first) out << ", ";
                out << "Volume{" << v << "[1]}";
                first = false;
            }
        }
        out << " Delete; };\n\n";
    }

    // -------- Physical groups -------------------------------------------
    out << "// physical volumes -- Elmer .sif references them by name\n";
    out << "Physical Volume(\"substrate\") = {" << sub_var << "[1]};\n";
    for (const auto& [ord, vars] : copper_vars_by_layer) {
        const auto* L = board.find_layer(ord);
        const std::string ln = L ? sanitise_layer_name(L->name)
                                  : ("ord" + std::to_string(ord));
        out << "Physical Volume(\"copper_" << ln << "\") = {";
        for (std::size_t i = 0; i < vars.size(); ++i) {
            if (i) out << ", ";
            out << vars[i] << "[1]";
        }
        out << "};\n";
    }

    if (!out) {
        return std::unexpected(GeoWriteError{"write failed on output stream"});
    }
    return {};
}

std::expected<void, GeoWriteError> write_board_geo(
    const board::Board& board,
    const std::filesystem::path& path,
    const GeoWriteOptions& opts) {
    std::ofstream f(path);
    if (!f) {
        return std::unexpected(GeoWriteError{
            "could not open " + path.string() + " for writing"});
    }
    return write_board_geo(board, f, opts);
}

}  // namespace circuitcore::formats::gmsh

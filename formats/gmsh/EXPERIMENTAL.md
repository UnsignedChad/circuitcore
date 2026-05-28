# experimental/conformal-mesh-export

KiCad PCB → Gmsh `.geo` → tet mesh → ElmerSolver thermal solve. Lives
on the `experimental/conformal-mesh-export` branch only; **not for
merge**.

## What works

* `pcb2geo board.kicad_pcb -o board.geo` emits a valid Gmsh .geo that
  uses the `OpenCASCADE` factory. Two kinds of entities ship today:
  * the Edge.Cuts polygon, extruded the full board thickness, tagged
    `Physical Volume("substrate")`.
  * each filled copper zone polygon (per copper layer), extruded by
    35 µm and stacked above / below the substrate. Per-layer Physical
    Volume tags like `copper_F_Cu`, `copper_B_Cu`, etc.
* `gmsh -3 board.geo -o board.msh -format msh22` runs to completion on
  pic_programmer (1 zone, 5 outline segs) → **260 358 nodes, 1.43 M
  elements** in ~60 s with "no ill-shaped tets in the mesh :-)".
* `ElmerGrid 14 2 board.msh -autoclean` picks up bodies and surface
  triangles cleanly. The .msh's `$PhysicalNames` survive: substrate +
  copper_B_Cu can be referenced by name from the .sif.
* `ElmerSolver case.sif` converges in ~45 s using UMFPACK direct
  solver. `case.vtu` opens in ParaView, temperature field is on the
  full 260 k nodes.

## Known limitations

1. **One self-intersecting polygon trips Gmsh.** KiCad's filled-zone
   boolean op sometimes emits a ring with a non-consecutive duplicate
   vertex (pinch point). `dedupe_ring` catches consecutive dupes;
   non-consecutive ones still produce a degenerate Curve Loop that
   Gmsh skips. One out of ~3 polygons on pic_programmer's B.Cu pour
   currently lands in the dropped pile -- the rest mesh fine.
   *Fix:* Clipper2 or CGAL-based pre-pass to clean rings before
   handing them to the .geo. Punted to a follow-up.

2. **Body numbering drift.** Gmsh Physical tags 1, 2 turn into Elmer
   bodies 1, 2 *only if no orphan elements exist*. With
   `Mesh.SaveAll = 1` (needed for boundary BCs) Gmsh exports unassigned
   elements too; ElmerGrid creates a `body3` for them. The .sif has to
   know about this -- see `case.sif` for the worked example.

3. **Thin copper + coarse mesh = boundary-locked.** Default 500 µm
   characteristic length gives ~1 tet through a 35 µm copper layer,
   so every copper node is a boundary node and Dirichlet BCs pin them
   all. Either drop `--cl` to ~30 µm (mesh balloons accordingly), or
   thicken the copper layer in `pcb2geo` options.

4. **Segments / pads / vias not yet exported.** Only zones (`filled`)
   are extruded. For most boards the zones cover the bulk of the
   copper area; signal traces are a small fraction by volume so the
   thermal answer is close. Real coverage will need:
   * traces → extruded rectangles per `board.segments`
   * vias → extruded cylinders (or hex columns) per `board.vias`
   * pads → polygon extrusions
   * `BooleanFragments` to merge overlaps -- which requires
     bookkeeping the new entity tags so Physical Volume directives
     point at the right post-fragmentation IDs.

5. **Outline cutouts dropped.** `chain_outline` finds all closed rings
   from the Edge.Cuts segments but only extrudes the largest. Mounting
   holes / connector slots inside the board boundary aren't
   subtracted yet.

## Reproducing the end-to-end run

```bash
# 1. Generate the .geo from a KiCad board
./build/formats/gmsh/pcb2geo \
    /usr/share/kicad/demos/pic_programmer/pic_programmer.kicad_pcb \
    -o /tmp/pic.geo

# 2. Tetrahedralise (~60 s)
gmsh -3 /tmp/pic.geo -o /tmp/pic.msh -format msh22

# 3. Convert to Elmer format (needs ElmerGrid from elmerfem)
mkdir -p /tmp/elmer_pic && cp /tmp/pic.msh /tmp/elmer_pic/
cd /tmp/elmer_pic
ElmerGrid 14 2 pic.msh -autoclean

# 4. Drop in a case.sif (see this dir's example) and solve
ElmerSolver case.sif

# 5. View case.vtu in ParaView
paraview /tmp/elmer_pic/pic/case0001.vtu
```

The `phystory/elmerfem` Docker image bundles gmsh + ElmerGrid +
ElmerSolver + paraview if you don't want to install them locally.

## Why not merge yet

This needs items #1 + #4 (clean rings, segments/vias/pads) before
it's worth offering to a v0.1 user. The substrate-only mesh is a
proof of life, not a feature.

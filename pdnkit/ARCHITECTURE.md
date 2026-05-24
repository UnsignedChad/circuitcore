# pdnkit architecture

A high-level map of the codebase for contributors and future-you.

## Layered design

```
┌─────────────────────────────────────────────────────────┐
│  Qt application shell  (src/)                           │
│    main.cpp                — entry + CLI11 parsing      │
│    MainWindow.{h,cpp}      — menus, status bar, docks  │
│    PcbCanvas.{h,cpp}       — QOpenGLWidget renderer     │
│    LayerPanel.{h,cpp}      — copper-layer visibility    │
│    AnalysisPanel.{h,cpp}   — IR-drop config + run       │
│    ColorLegend.{h,cpp}     — viridis scale bar          │
└────────────────────────────┬────────────────────────────┘
                             │ uses
                             ▼
┌─────────────────────────────────────────────────────────┐
│  pdnkit_core static library  (src/{sexpr,model,...})    │
│  No Qt dependency. Linked by GUI binary AND tests.      │
│                                                         │
│    sexpr/     — generic S-expression parser             │
│    model/     — Board, Layer, Net, Pad, Zone... + Hit   │
│    parser/    — .kicad_pcb → model::Board               │
│    pi/        — IrMesher, IrSolver (the actual PI)      │
│    render/    — Camera2D, mesh tessellation, colors,    │
│                 IrResultMesh                            │
└─────────────────────────────────────────────────────────┘
                             │ depends on
                             ▼
        Eigen3 · spdlog · CLI11 · earcut.hpp (vendored)
```

Two crucial properties of the split:

1. **`pdnkit_core` has no Qt dependency.** Anything that doesn't need a
   window lives here — the parser, the model, the math. This keeps the
   library easy to unit-test (Catch2 just links it), reusable from headless
   CLI (`--analyze`), and portable if we ever target a non-Qt frontend.

2. **Qt code lives in `src/` flat (next to `main.cpp`)** rather than in a
   subdirectory. Smaller code-base; promotes when it's worth it.

## Data flow

The whole pipeline from `.kicad_pcb` to a heat-map on screen:

```
file.kicad_pcb
      │ KicadPcbParser::parse_file
      ▼
sexpr::Node tree        (parsed S-expression)
      │ Walker (in KicadPcbParser.cpp)
      ▼
model::Board            (typed; SI units everywhere)
      │
      ├─→ render::build_all_meshes()    ZoneMesher + SegmentMesher
      │       │                          + ViaMesher + PadMesher
      │       ▼
      │   vector<LayerMesh>             (GL-ready triangles per layer)
      │       │ PcbCanvas::uploadBoardMeshes
      │       ▼
      │   board GPU buffer              (one VBO/IBO, layer ranges)
      │
      └─→ pi::IrMesher::build(MeshConfig)
              │
              ▼
          pi::IrMesh           (nodes + resistors + node_currents)
              │ pi::IrSolver::solve
              ▼
          pi::Solution         (per-node voltage vector)
              │ render::build_ir_result_mesh
              ▼
          render::IrResultMesh (quad per node, voltage normalized)
              │ PcbCanvas::uploadIrResult
              ▼
          heat-map GPU buffer  (drawn by heat_prog_ with viridis)
```

## Conventions

- **Units:** SI throughout (meters, radians, Amperes, Volts). KiCad input mm
  and degrees are converted at parse time so downstream code never deals with
  unit mixups.
- **Coordinate system:** KiCad's Y grows downward (screen convention). The
  parser preserves this; `Camera2D::ortho_matrix` flips Y in the projection so
  KiCad-style coordinates display correctly in OpenGL's Y-up clip space.
- **Naming:** types in `PascalCase`, functions in `snake_case`, members with
  trailing underscore. Namespaces lowercase: `pdnkit::sexpr`, `pdnkit::model`,
  `pdnkit::parser`, `pdnkit::pi`, `pdnkit::render`, `pdnkit::hittest`.
- **Memory:** raw pointers for non-owning Qt parent/child relationships (the
  Qt convention); `unique_ptr` for owned heap allocations crossing scopes;
  values otherwise.
- **Error handling:** parser/solver use exceptions for invalid input; the GUI
  catches at the action boundary and shows `QMessageBox`. Pure-data functions
  prefer returning a "Solution" struct with `ok + error` rather than throwing.

## How a feature lands

Typical anatomy of a new feature commit:

1. **Domain code first.** Add types/functions in `pdnkit_core` (no Qt).
2. **Tests in `tests/`.** Catch2 v3, one file per logical unit. Cover happy
   paths and one or two error cases.
3. **CMake wiring.** Add `.cpp` to `add_library(pdnkit_core)` in root
   `CMakeLists.txt`, and tests to `tests/CMakeLists.txt`.
4. **GUI plumbing.** Add a menu action / panel control / signal to MainWindow
   or one of the panels. Connect it to the new core function. Wrap with a
   `try { } catch (const std::exception& e) { QMessageBox::critical(...); }`.
5. **Build + ctest.** Both must be green before commit.
6. **Commit with focused message.** One logical unit per commit; push as you
   go (see `feedback_commit_cadence` notes).

## Math reference

- **Static IR drop:** sparse Cholesky on the nodal-conductance matrix
  `G·v = i`, with sink nodes pinned via large-diagonal stiffness so `G` stays
  SPD. Conductance per resistor for square cells is the sheet-conductance
  `t / ρ` — independent of grid spacing — see Wadell, *Transmission Line
  Design Handbook*, §3.2.
- **Future Z(f):** segmentation method (Okoshi 1972, refined by IBM /
  Swaminathan in the 90s) plus cavity model for plane resonances. Reference:
  Swaminathan, *Power Integrity Modeling and Design for Semiconductors and
  Systems*.

## Build system

- CMake ≥ 3.25, single root `CMakeLists.txt` + a `tests/CMakeLists.txt`.
- Two CMake targets: `pdnkit_core` (static lib) and `pdnkit` (GUI binary).
- C++20, warnings via `-Wall -Wextra -Wpedantic`.
- clang++ is the recommended compiler; CI builds both clang++ and g++.
- Test discovery: Catch2's `catch_discover_tests` registers each test case
  with CTest, so `ctest --output-on-failure` is the canonical test command.
- Vendored: `third_party/mapbox/earcut.hpp` (ISC), included from the
  `third_party/` path that `pdnkit_core` exposes via `target_include_directories`.

## CI

`.github/workflows/build.yml` runs on every push and PR:
- Ubuntu 24.04 runner, matrix over `gcc` and `clang`
- `apt install` the deps (same list as `README.md`)
- `cmake -B build -G Ninja`, `cmake --build build`, `ctest`

If you change a system dep, update both `README.md` and the workflow.

## Test fixtures

`tests/fixtures/tiny_pdn.kicad_pcb` is a hand-written but real-format
KiCad PCB with two-layer copper, multiple nets, through-hole + SMD pads,
and filled zones — small enough to read by hand, broad enough to exercise
the parser corners that matter. `tests/e2e_test.cpp` loads it via
`KicadPcbParser::parse_file` and runs the full pipeline.

If you fix a parser bug against a real KiCad file, distill the minimum
reproduction into a new fixture under `tests/fixtures/` and a test that
asserts the fix. Real PCBs are typically too large/proprietary to vendor.

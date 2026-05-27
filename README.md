# circuitcore

PCB analysis toolkit. C++23, Qt6, Eigen, SuiteSparse, optional VTK 9 for
3D field rendering. GPL-3.0.

Four analysis tools share one canonical board model parsed from
`.kicad_pcb`:

- **pdnkit** -- power integrity
- **sikit** -- signal integrity
- **emikit** -- EMI / radiated emissions
- **mpkit** -- multiphysics (thermal, elasticity, coupled studies)
- **studio** -- shared shell that hosts all four in tabs

```
board/         canonical PCB model
sexpr/         s-expression read + emit
field/         shared 3D Field / Grid types
formats/kicad/ .kicad_pcb -> board::Board
netlist/       KiCad netlist reader
intent/        board vs schematic cross-checks
ui/            shared Qt canvas + camera
pdnkit/
sikit/
emikit/
mpkit/
studio/
```

File formats live under `formats/`. Analysis code only sees `board::Board`.

---

## pdnkit (power integrity)

- Static IR drop. Multi-layer, vias couple planes. CHOLMOD via SuiteSparse.
- Track-based 1D IR drop for pour-less boards.
- IPC-2152 trace-current DRC.
- Cavity-model Z(f) for rectangular planes plus segmentation for arbitrary shapes.
- Decoupling caps in Z(f) with Ruehli mounting inductance, DC-bias derating, vendor-realistic library.
- Decap auto-suggest and leave-one-out sensitivity.
- Target-Z generator from rail spec.
- Time-domain transient solver, per-node C.
- Plane resonance mode-shape visualization.
- Via inductance (Grover / Ruehli partial).
- Djordjevic-Sarkar wideband dielectric.
- Hammerstad-Jensen surface roughness, wired into the cavity model.
- VRM output impedance overlay on Z(f).
- IR + thermal coupling (temperature-corrected sigma, fixed-point loop).
- Model order reduction via Schur complement, reduced SPICE export.
- SPICE netlist export, Touchstone .s1p writer.
- HTML signoff report (board summary, IR, DRC, Z(f), verdict).

**CLI**: `--analyze`, `--zf`, `--touchstone`, `--transient`, `--drc`, `--probe-r`, `--target-z`, `--list-nets`, `--list-layers`, `--version`.

## sikit (signal integrity)

- Trace impedance: Wadell closed-form plus in-house FDM solver.
- Cross-section / stackup model.
- S-parameter read + write (Touchstone .s2p), CSV.
- Vector fitting, rational SPICE export.
- Eye diagrams: time-domain Eye, StatEye, EyeMetrics, EyeMask, Bathtub.
- IBIS parser, IBIS-AMI support.
- Diff pair synthesis and analysis.
- Crosstalk, skew, bus-skew.
- Return-path analysis.
- Via model.
- FDTD3D solver with PEC rasterizer and lumped port for S-param extraction.
- Connector model, channel synthesis + response.
- Built-in compliance specs (`list-specs`), de-embedding, comparison report.
- Djordjevic-Sarkar + HJ roughness.

**CLI**: `impedance`, `touchstone`, `spice`, `compliance`, `deembed`, `compare`, `skew`, `bus-skew`, `return-path`, `report`, `derive-topology`, `fdtd info`, `list-specs`, `list-nets`.

## emikit (EMI)

- Loop-emission estimator (di/dt over loop area).
- Cable common-mode model.
- Calibration against TI ADS8686S chamber data (see `emikit/VALIDATION.md`).
- Mask check against built-in EMC specs.

**CLI**: `check`, `list-masks`.

## mpkit (multiphysics)

- Voxel rasteriser turns a parsed board into a 3D material grid.
- Steady-state 3D heat solver, finite-volume on the voxel grid,
  harmonic-mean conductivity across material interfaces, sparse
  Cholesky.
- Transient 3D heat solver, implicit backward Euler.
- Linear elasticity solver, Q1 hex FEM, body force + thermal-strain
  source.
- Joule coupling: pdnkit IR solution -> volumetric heat source.
- Thermoelectric post-processor: Seebeck, Peltier, Thomson primitives;
  Material gains Seebeck coefficients for the standard thermocouple +
  TE-cooler metals (chromel, alumel, constantan, bismuth, ...).
- Study orchestrator: a Study tree of physics nodes + couplings is
  walked end-to-end. Multi-parameter full-factorial sweeps. Persisted
  as `.mpstudy` sexpr plus binary `.mpfield` sidecars.
- All solvers verified against analytical solutions
  (1-D linear / parabolic / series-slab conduction; closed-form
  first-mode decay; uniaxial compression; hydrostatic thermal
  constraint; Type-K thermocouple sensitivity).

## studio

One window, five tabs (Board / SI / PI / EMI / Mp), single shared
board. Drop a `.kicad_pcb` and click between PI / SI / EMI / Mp without
re-loading.

The Mp tab uses VTK 9 via `QVTKOpenGLNativeWidget` for the 3D viewer
(orbit / pan / zoom camera, XYZ gizmo, axis-aligned slice plane,
colormap legend, four built-in colormaps). VTK is built from source via
CMake `FetchContent` against Qt6, since the Debian apt package ships
against Qt5 and silently mismatches. First configure on a fresh machine
takes ~15-20 min; subsequent builds reuse the cached build tree.

---

## Build (Ubuntu 24.04)

```
sudo apt install -y qt6-base-dev qt6-base-dev-tools \
    libqt6opengl6-dev libqt6openglwidgets6 \
    libeigen3-dev libsuitesparse-dev libcgal-dev \
    libspdlog-dev libcli11-dev libboost-dev \
    libopenmpi-dev \
    ninja-build cmake clang-18 catch2

CC=clang-18 CXX=clang++-18 cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
```

To skip the VTK build (the Mp tab loses the 3D viewer; everything else
is unchanged):

```
cmake -B build -G Ninja -DCIRCUITCORE_BUILD_MPKIT_WIDGETS=OFF
```

Binaries land in `build/pdnkit/pdnkit`, `build/sikit/sikit`,
`build/emikit/emikit`, `build/studio/circuitcore_studio`.

---

## Validation

- 700+ unit + integration tests across all kits.
- `pdnkit/tests/` -- Ohms-law fixture, Wadell verification, full IR +
  cavity + transient + sensitivity pipeline test.
- `mpkit/tests/` -- analytical conduction, parabolic with source,
  series copper/FR-4 slab, first-cosine decay, uniaxial compression,
  hydrostatic thermal constraint, Type-K thermocouple sensitivity.
- `emikit/VALIDATION.md` -- TI ADS8686S chamber-data correlation.
- Sikit FDTD validated against analytic stripline + planar-waveguide
  cases.

## Contributing

See `CONTRIBUTING.md`. CI runs gcc-13 on Ubuntu 24.04. CODEOWNERS gates
shared modules. Auto-merge on green.

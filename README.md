# circuitcore

PCB analysis toolkit. C++23, Qt6, Eigen, SuiteSparse. GPL-3.0.

Three tools share one canonical board model parsed from `.kicad_pcb`:

- **pdnkit** - power integrity
- **sikit** - signal integrity
- **emikit** - EMI / radiated emissions
- **studio** - shared shell that hosts all three in tabs

```
board/         canonical PCB model
sexpr/         s-expression read + emit
formats/kicad/ .kicad_pcb -> board::Board
netlist/       KiCad netlist reader
ui/            shared Qt canvas + camera
pdnkit/
sikit/
emikit/
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

**GUI**: stackup editor with save-back to a new `.kicad_pcb`, layer / net / DRC / cavity / net-stats / transient docks, voltage and current-density heatmaps, hotspot marker, live cursor probe, right-click probe-R, decap markers on canvas, drag-and-drop, recent files.

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

## studio

One window, tabs for each tool, single shared board. Drop a `.kicad_pcb` and click between PI / SI / EMI without re-loading.

---

## Build (Ubuntu 24.04)

```
sudo apt install -y qt6-base-dev qt6-base-dev-tools \
    libqt6opengl6-dev libqt6openglwidgets6 \
    libeigen3-dev libsuitesparse-dev libcgal-dev \
    libspdlog-dev libcli11-dev libboost-dev \
    ninja-build cmake clang-18 catch2

CC=clang-18 CXX=clang++-18 cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Binaries land in `build/pdnkit/pdnkit`, `build/sikit/sikit`, `build/emikit/emikit`, `build/studio/studio`.

---

## Validation

- `pdnkit/tests/` - 660+ unit + integration tests, Ohms-law fixture, Wadell verification, full Tier1+Tier2 pipeline test.
- `emikit/VALIDATION.md` - TI ADS8686S chamber-data correlation.
- Sikit FDTD validated against analytic stripline + planar-waveguide cases.

## Contributing

See `CONTRIBUTING.md`. CI runs gcc-13 on Ubuntu 24.04. CODEOWNERS gates shared modules. Auto-merge on green.

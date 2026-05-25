# Studio GUI coverage

Where every public sikit / pdnkit / emikit capability surfaces in
`circuitcore_studio`. Built by walking the public headers + CLI
subcommands + READMEs and cross-referencing the four studio tabs.

Legend:
- **studio** -- reachable from the GUI today
- **CLI-only** -- intentional, no GUI surface planned
- **GAP** -- should be in the GUI; follow-up task filed

## sikit (Signal Integrity)

### Analyses
| Capability                                          | Where in studio                          | Status |
|-----------------------------------------------------|-------------------------------------------|--------|
| Single-ended channel synthesis (`synthesize_channel`)| SI tab toolbar -> "Plot S-param"          | studio |
| Diff-pair channel synthesis (`synthesize_diff_channel`)| SI tab toolbar -> "Plot diff-pair"      | studio |
| Via 2-port (`compute_via_s2p`)                       | SI tab toolbar -> "Plot via"              | studio |
| Per-segment impedance (`compute_all`)                | SI tab Z=50/90/100 overlay buttons        | studio |
| Diff-pair impedance (`compute_diff_pairs`)           | SI tab Zdiff=90/100 buttons               | studio |
| Eye synth (`prbs7 + nrz_waveform + build_eye`)       | SI tab toolbar -> "Synth eye"             | studio |
| Statistical eye / PDA (`StatEye`)                    | -- not wired                              | GAP    |
| Multi-aggressor crosstalk (`Crosstalk`)              | -- not wired                              | GAP    |
| Rational fit + SPICE export (`VectorFit`, `SpiceExport`)| -- not wired                            | GAP    |
| Compliance suites (`Compliance`, `EyeMask`)          | sikit CLI `compliance` subcommand         | CLI-only (auto-checked in eye render) |
| De-embedding (`SParam::deembed`)                     | sikit CLI `deembed` subcommand            | GAP    |
| Touchstone overlay measured-vs-sim (`Overlay`)       | sikit CLI `compare` subcommand            | GAP    |
| Length skew (`Skew`, `BusGroup`)                     | sikit CLI `skew`, `bus-skew`              | GAP    |
| Return-path detector (`ReturnPath`)                  | sikit CLI `return-path`                   | GAP    |
| HTML compliance report (`Report`)                    | sikit CLI `report`                        | GAP    |
| Schematic-derived topology (`SchematicTopology`)     | sikit CLI `derive-topology`               | GAP    |
| Connector preset library (`Connector`)               | -- not surfaced                           | GAP    |
| Topology cascade (`Topology` ChannelBlock list)      | -- model only, no editor                  | GAP    |
| 3D FDTD (`Fdtd3d`, `FdtdPort`, `FdtdRasterize`)      | sikit CLI `fdtd info`                     | CLI-only (run is multi-minute; status doc says CLI-only by design) |

### File formats
| Capability                                         | Where                                       | Status |
|----------------------------------------------------|---------------------------------------------|--------|
| Touchstone read / write (`Touchstone*`)            | SI tab Export .s2p; also CLI `touchstone`   | studio |
| TouchstoneCSV (`TouchstoneCsv`)                    | SI tab Export .csv                          | studio |
| Project save/load (`Project`)                      | -- not wired in studio                      | GAP    |
| IBIS reader (`Ibis`)                               | SI tab "Open IBIS"                          | studio |
| AMI loader (`Ami`)                                 | SI tab "Open AMI"                           | studio |

### Math kernels (not user-facing)
- `Bathtub`, `EyeMetrics`, `EyeMask`, `ChannelResponse`, `Fft`,
  `CrossSection`, `DjordjevicSarkar`, `FdmSolver`, `FdmGrid`, `SiStackup`,
  `Rlgc`, `SurfaceRoughness`, `TraceImpedance`, `ViaModel`, `DiffPair`,
  `DiffSynth`, `Impedance` -- internal building blocks consumed by the
  higher-level analyses above. No direct GUI surface needed.

## pdnkit (Power Integrity)

### Top-level analyses (CLI flag -> GUI)
| CLI flag         | Capability                              | Where in studio                              | Status |
|------------------|------------------------------------------|-----------------------------------------------|--------|
| `--analyze`      | Static IR drop (`IrMesher` + `IrSolver`) | PI tab AnalysisPanel + Run                    | studio |
| `--zf`           | Cavity Z(f) sweep (`CavityModel`)        | PI tab CavityPanel                            | studio |
| `--probe-r`      | Pad-to-pad probe resistance              | PI tab right-click probe-R workflow           | studio |
| `--transient`    | Transient IR (`Transient`)               | PI tab TransientPanel                         | studio |
| `--drc`          | IPC-2152 power DRC (`PowerDrc`)          | PI tab DrcPanel                               | studio |
| `--spice`        | SPICE subcircuit (`SpiceExport`)         | -- not wired                                  | GAP    |
| `--touchstone`   | One-port Z(f) Touchstone                 | -- not wired                                  | GAP    |
| `--list-nets`    | Net listing                              | implicit via NetStatsPanel                    | studio |
| `--list-layers`  | Layer listing                            | implicit via LayerPanel                       | studio |

### Calculator-style flags (no board required)
| Flag         | Calculator                                | Status |
|--------------|-------------------------------------------|--------|
| `--target-z` | Target impedance from (V_nom, V_tol, I)   | GAP (calculator panel) |
| `--via-l`    | Via inductance from diameter + length     | GAP (calculator panel) |
| `--vrm-z`    | VRM impedance from R + L + f              | GAP (calculator panel) |
| `--eps-f`    | Djordjevic-Sarkar eps_r at frequency      | GAP (calculator panel) |
| `--rough-k`  | Hammerstad-Jensen roughness factor        | GAP (calculator panel) |
| `--thermal`  | Thermal-aware IR (`Thermal`)              | partial (toggle on AnalysisPanel)             |

### Higher-level features (no CLI flag)
| Capability                                           | Where                                  | Status |
|------------------------------------------------------|----------------------------------------|--------|
| Decap optimizer (`DecapOptimizer`)                   | -- not wired                           | GAP    |
| Sensitivity / leave-one-out (`Sensitivity`)          | -- not wired                           | GAP    |
| Model-order reduction (`Mor`)                        | -- not wired                           | GAP    |
| Dielectric model (`Dielectric`)                      | used internally by CavityPanel         | studio (transparent) |
| Save modified stackup back to .kicad_pcb (`StackupWriter`) | standalone pdnkit File menu        | GAP (move to studio File menu) |
| Save canvas as image                                 | standalone pdnkit File menu            | GAP    |
| Export results as CSV                                | standalone pdnkit File menu            | GAP    |

## emikit (EMI / EMC)

| Capability                                  | Where in studio                        | Status |
|---------------------------------------------|----------------------------------------|--------|
| Board emissions analyzer (`analyze_board`)  | EMI tab Run compliance                 | studio |
| Trapezoidal drive spectrum (`Spectrum`)     | EMI tab drive spinboxes                | studio |
| CISPR / FCC masks (`Masks`)                 | EMI tab mask combo                     | studio |
| Loop emissions formula (`loop_e_field`)     | used internally + emikit calibration CLI| CLI-only (math kernel) |
| Cable common-mode (`cable_cm_e_field`)      | -- not wired                            | GAP (cable-CM calculator panel) |
| TI ADS8686S validation tool                 | `emikit_validate_ti` standalone binary | CLI-only (validation script) |

## studio shell

| Capability                                  | Status |
|---------------------------------------------|--------|
| Drag-and-drop .kicad_pcb open               | GAP (have File>Open, no drop)          |
| Recent files                                | GAP                                    |
| Per-board settings persistence              | GAP                                    |
| Keyboard shortcuts dialog                   | GAP                                    |
| Save canvas as image                        | GAP                                    |

## Follow-up tasks to file

Grouped to keep the queue manageable. Listed in suggested priority order.

1. **SI tab: HTML report + de-embed + compare + skew + return-path + derive-topology**
   -- These are CLI-only today but the analyses are useful interactively. One PR per group of related workflows.
2. **SI tab: statistical eye + multi-aggressor crosstalk + SPICE export + connector library**
   -- More specialized SI features, fits a "Tools" menu on the SI tab.
3. **PI tab: SPICE / Touchstone export buttons + save modified stackup**
   -- Three buttons or a File submenu on the PI tab.
4. **PI tab: decap optimizer + sensitivity + MOR**
   -- Three pdnkit features that have no GUI today; could be a single "Optimize" submenu.
5. **Calculator panels** (pdnkit `--target-z` `--via-l` `--vrm-z` `--eps-f` `--rough-k`, emikit cable CM)
   -- A "Calculators" menu in the studio shell (board-independent helpers).
6. **studio shell: drag-and-drop, recent files, per-board settings, save canvas as image, shortcuts dialog**
   -- Standard polish, one PR.

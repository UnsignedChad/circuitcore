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
| Statistical eye / PDA (`StatEye`)                    | SI tab Tools -> Statistical eye (PDA)     | studio |
| Multi-aggressor crosstalk (`Crosstalk`)              | SI tab Tools -> Crosstalk eye for victim  | studio |
| Rational fit + SPICE export (`VectorFit`, `SpiceExport`)| SI tab Tools -> Export channel as SPICE | studio |
| Compliance suites (`Compliance`, `EyeMask`)          | sikit CLI `compliance` subcommand         | CLI-only (auto-checked in eye render) |
| De-embedding (`SParam::deembed`)                     | SI tab toolbar -> De-embed                | studio |
| Touchstone overlay measured-vs-sim (`Overlay`)       | SI tab toolbar -> Compare overlay         | studio |
| Length skew (`Skew`, `BusGroup`)                     | SI tab toolbar -> Skew                    | studio |
| Return-path detector (`ReturnPath`)                  | SI tab toolbar -> Return path             | studio |
| HTML compliance report (`Report`)                    | SI tab toolbar -> HTML report             | studio |
| Schematic-derived topology (`SchematicTopology`)     | SI tab toolbar -> Topology from .net      | studio |
| Connector preset library (`Connector`)               | SI tab Tools -> Connector library         | studio |
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
| `--spice`        | SPICE subcircuit (`SpiceExport`)         | PI tab File -> Export reduced SPICE           | studio |
| `--touchstone`   | One-port Z(f) Touchstone                 | PI tab File -> Export cavity Z(f) Touchstone  | studio |
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
| Decap optimizer (`DecapOptimizer`)                   | PI tab Cavity panel -> Auto-suggest    | studio |
| Sensitivity / leave-one-out (`Sensitivity`)          | PI tab Cavity panel -> Decap sensitivity | studio |
| Model-order reduction (`Mor`)                        | PI tab File -> Export reduced SPICE    | studio |
| Dielectric model (`Dielectric`)                      | used internally by CavityPanel         | studio (transparent) |
| Save modified stackup back to .kicad_pcb (`StackupWriter`) | PI tab File -> Save modified stackup | studio |
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

Shipped since this audit was first written:
- SI tab toolbar: HTML report, de-embed, compare overlay, skew, return path, derive-topology.
- SI tab Tools menu: statistical eye (PDA), crosstalk eye, SPICE export, connector library.
- PI tab File menu: save modified stackup, export cavity Z(f) Touchstone, export reduced SPICE.
- Decap optimizer and leave-one-out sensitivity are reachable on the PI tab Cavity panel.

Open:

1. **Calculator panels** (pdnkit `--target-z` `--via-l` `--vrm-z` `--eps-f` `--rough-k`, emikit cable CM)
   -- A "Calculators" menu in the studio shell (board-independent helpers).
2. **studio shell: drag-and-drop, recent files, per-board settings, save canvas as image, shortcuts dialog**
   -- Standard polish, one PR.

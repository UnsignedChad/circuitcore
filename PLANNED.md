# planned features

Scratchpad for ideas, not commitments. Order is rough priority --
shipped items get crossed out and dropped, follow-ups for what just
landed sit near the top.

1. **mpkit Mp tab study editor** -- the Mp tab today hard-codes a
   PDN -> thermal demo workflow. Wire it through `mpkit::run_study` so
   users can compose Studies in the GUI (add physics node, add
   coupling, edit BC table, hit Run) and persist them as `.mpstudy`
   files via the already-shipped serialiser.
2. **mpkit TransientHeat + Elasticity in the orchestrator** -- both
   solvers ship; `run_study` only dispatches `SteadyHeat` so far.
   Add the missing cases + parse their config sexpr.
3. **mpkit coupled electric-thermal solver** -- new `Thermoelectric`
   PhysicsKind that calls the post-processor primitives internally,
   does the nonlinear fixed-point iteration where current depends on
   `grad(T)` via Ohm + Seebeck and the heat equation gains Peltier +
   Thomson source terms.
4. **mpkit viewer v2** -- iso-surfaces (vtkContourFilter), GPU volume
   rendering (vtkGPUVolumeRayCastMapper), arbitrary slice plane
   (vtkCutter), vector arrows + streamlines. The widget API is
   already shaped for these; just more code behind the same calls.
5. **VTK in the release CI matrix** -- Linux/macOS/Windows release legs
   need VTK to ship the studio binary with the Mp tab. FetchContent +
   Actions cache for the build artefacts; same approach the SuiteSparse
   wrangling settled on.
6. **emikit multi-net + activity factor** -- closes the test 3 chamber
   gap. Today `cfg.drive` is one trapezoidal per board; multi-source
   with per-net duty / burst would land tests within +-5 dB everywhere.
7. **PCIe / DDR / USB built-in compliance templates** -- sikit already
   has the `compliance` infra and `list-specs`. Mostly data, not code.
8. **CI mode** -- `circuitcore-ci board.kicad_pcb --config
   .circuitcore.toml` that runs PI + SI + EMI + Mp headlessly, exits
   non-zero on regression. Pairs with the existing HTML signoff report.
9. **drckit** -- new sister tool. Geometric + electrical DRC
   (clearance, annular ring, hole size, copper-to-edge, current-too-
   high, missing return path). Extends `intent`.
10. **schematic viewer in studio** -- `.kicad_sch` already parses; add a
    SchemTab next to BoardTab. Cross-probe net selection.
11. **stripline shielding in emikit** -- distinguish inner-layer signals
    (shielded by both planes, 20-30 dB less radiation) from microstrip.
12. **PAM-4 eye + FFE / DFE / CTLE** in sikit -- extends Eye / StatEye
    stack.
13. **frequency-domain transient** in pdnkit -- FFT-based decap
    response, much faster than time-domain transient for sweeps.
14. **AC noise margin verdict** -- combine cavity + IR + thermal into
    one worst-case droop number, plot vs frequency.
15. **web export** -- render the existing HTML signoff to a
    self-contained shareable artefact (no live server).
16. **analogkit** -- board-level analog. Pull the analog sub-circuit
    out of the schematic, pipe to ngspice (BSD), run DC op-point + AC
    sweep + noise + Monte Carlo, then tie back to PCB layout effects
    (e.g. feedback trace parasitic L degrades gain). LTspice can do
    the sim part but does not know about your PCB; that is the angle.
17. **biasingkit** -- DC + AC bias analysis from schematic. SPICE-lite
    for the analog blocks on a mostly-digital board.
18. **multi-board / connector system** -- pdnkit treats one board
    today; real designs are mainboard + daughter + cable.
19. **3D mechanical (STEP)** -- clearance, enclosure shielding factor,
    mounted-component CG for vibe analysis.
20. **routing optimiser** -- auto-route or auto-rip-up to minimise a
    weighted PI + SI + EMI + thermal score. Whole new dimension.
21. **python bindings** -- pybind11 wrappers for the analysis libs.
22. **diff mode** -- load board A vs board B, show what got worse.
23. **temperature-dependent material properties** -- Material gains
    callbacks like `k(T)`, `S(T)`. Lets the Thomson coefficient
    (`tau = T * dS/dT`) be non-zero and lets pdnkit thermal couple
    properly with mpkit's per-cell solver instead of the lumped
    approximation.
24. **anisotropic materials** -- orthotropic E_x / E_y / E_z and
    k_x / k_y / k_z for FR-4. Matters for warpage prediction and
    long-trace impedance shifts.

## research / validation

- More EMI reference pairs (Hockanson 1996 test board, IEC 61967 IC).
- pdnkit cavity vs published measurement.
- Finish openEMS cross-check (PR #62 WIP).
- mpkit thermal vs commercial reference (Icepak, Sherlock) on a
  published high-current board.

## not in scope

- **IC-level analog / RF** -- different industry, different file
  formats and PDKs, different users. That is the Cadence Virtuoso /
  Xschem + SKY130 / IHP130 world. circuitcore is board-level.

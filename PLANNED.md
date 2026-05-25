# planned features

scratchpad for ideas, not commitments. rough order of interest.

1. **drckit** -- new sister tool. geometric + electrical DRC (clearance,
   annular ring, hole size, copper-to-edge, current-too-high, missing
   return path). extends `intent`.
2. **emikit multi-net + activity factor** -- closes the test 3 chamber
   gap. today `cfg.drive` is one trapezoidal per board; multi-source
   with per-net duty/burst would land tests within +-5 dB everywhere.
3. **PCIe / DDR / USB built-in compliance templates** -- sikit already
   has the `compliance` infra and `list-specs`. mostly data, not code.
4. **CI mode** -- `circuitcore-ci board.kicad_pcb --config
   .circuitcore.toml` that runs PI+SI+EMI headlessly, exits non-zero on
   regression. pairs with the existing report HTML.
5. **schematic viewer in studio** -- `.kicad_sch` already parses; add a
   SchemTab next to BoardTab. cross-probe net selection.
6. **stripline shielding in emikit** -- distinguish inner-layer signals
   (shielded by both planes, 20-30 dB less radiation) from microstrip.
7. **PAM-4 eye + FFE/DFE/CTLE** in sikit -- extends Eye/StatEye stack.
8. **frequency-domain transient** in pdnkit -- FFT-based decap response,
   much faster than time-domain transient for sweeps.
9. **AC noise margin verdict** -- combine cavity + IR + thermal into one
   worst-case droop number, plot vs frequency.
10. **web export** -- render the existing HTML signoff to a self-contained
    shareable artifact (no live server).
11. **analogkit** -- board-level analog. pull analog sub-circuit out of
    the schematic, pipe to ngspice (BSD), run DC op-point + AC sweep +
    noise + Monte Carlo, then tie back to PCB layout effects (e.g.
    feedback trace parasitic L degrades gain). LTspice can do the sim
    part but does not know about your PCB; that is the angle.
12. **rfkit** -- board-level RF. smith chart, matching network synthesis
    (L / pi / T), antenna param extraction from S-params (S11, return
    loss, bandwidth), Rollet K stability + mu-factor, noise figure
    cascading, group delay. bones already there: FDTD3D, S-param math,
    vector fitting. could be a sikit extension or sister tool.
13. **biasingkit** -- DC + AC bias analysis from schematic. SPICE-lite for
    the analog blocks on a mostly-digital board.
14. **multi-board / connector system** -- pdnkit treats one board today;
    real designs are mainboard + daughter + cable.
15. **3D mechanical (STEP)** -- clearance, enclosure shielding factor.
16. **routing optimizer** -- auto-route or auto-rip-up to minimize a
    weighted PI+SI+EMI score. whole new dimension.
17. **python bindings** -- pybind11 wrappers for the analysis libs.
18. **diff mode** -- load board A vs board B, show what got worse.
19. more EMI reference pairs (Hockanson 1996 test board, IEC 61967 IC).
20. pdnkit cavity vs published measurement.
21. finish openEMS cross-check (PR #62 WIP).

## not in scope

- **IC-level analog / RF** -- different industry, different file formats
  and PDKs, different users. that is the Cadence Virtuoso / Xschem +
  SKY130 / IHP130 world. circuitcore is board-level.

# planned features

scratchpad for ideas, not commitments. order is rough priority.

## tier 1 -- high value, modest effort

- **drckit** -- new sister tool. geometric + electrical DRC (clearance,
  annular ring, hole size, copper-to-edge, current-too-high, missing
  return path). extends `intent`.
- **emikit multi-net + activity factor** -- closes the test 3 chamber
  gap. today `cfg.drive` is one trapezoidal per board; multi-source
  with per-net duty/burst would land tests within +-5 dB everywhere.
- **PCIe / DDR / USB built-in compliance templates** -- sikit already
  has the `compliance` infra and `list-specs`. mostly data, not code.
- **CI mode** -- `circuitcore-ci board.kicad_pcb --config
  .circuitcore.toml` that runs PI+SI+EMI headlessly, exits non-zero on
  regression. pairs with the existing report HTML.

## tier 2 -- high value, more effort

- **schematic viewer in studio** -- `.kicad_sch` already parses; add a
  SchemTab next to BoardTab. cross-probe net selection.
- **stripline shielding in emikit** -- distinguish inner-layer signals
  (shielded by both planes, 20-30 dB less radiation) from microstrip.
- **PAM-4 eye + FFE/DFE/CTLE** in sikit -- extends Eye/StatEye stack.
- **frequency-domain transient** in pdnkit -- FFT-based decap response,
  much faster than time-domain transient for sweeps.
- **AC noise margin verdict** -- combine cavity + IR + thermal into one
  worst-case droop number, plot vs frequency.
- **web export** -- render the existing HTML signoff to a self-contained
  shareable artifact (no live server).
- **analogkit** -- board-level analog. pull analog sub-circuit out of
  the schematic, pipe to ngspice (BSD), run DC op-point + AC sweep +
  noise + Monte Carlo, then tie back to PCB layout effects (e.g.
  feedback trace parasitic L degrades gain). LTspice can do the sim
  part but does not know about your PCB; that is the angle.
- **rfkit** -- board-level RF. smith chart, matching network synthesis
  (L / pi / T), antenna param extraction from S-params (S11, return
  loss, bandwidth), Rollet K stability + mu-factor, noise figure
  cascading, group delay. bones already there: FDTD3D, S-param math,
  vector fitting. could be a sikit extension or sister tool.

## tier 3 -- strategic / new tools

- **biasingkit** -- DC + AC bias analysis from schematic. SPICE-lite for
  the analog blocks on a mostly-digital board.
- **multi-board / connector system** -- pdnkit treats one board today;
  real designs are mainboard + daughter + cable.
- **3D mechanical (STEP)** -- clearance, enclosure shielding factor.
- **routing optimizer** -- auto-route or auto-rip-up to minimize a
  weighted PI+SI+EMI score. whole new dimension.
- **python bindings** -- pybind11 wrappers for the analysis libs.
- **diff mode** -- load board A vs board B, show what got worse.

## research / validation

- more EMI reference pairs (Hockanson 1996 test board, IEC 61967 IC).
- pdnkit cavity vs published measurement.
- finish openEMS cross-check (PR #62 WIP).

## not in scope

- **IC-level analog / RF** -- different industry, different file formats
  and PDKs, different users. that is the Cadence Virtuoso / Xschem +
  SKY130 / IHP130 world. circuitcore is board-level.

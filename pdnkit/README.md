# pdnkit

Power Integrity for KiCad PCBs. Static IR drop, plane Z(f), time-domain
transient, IPC-2152 DRC, decap optimizer.

## GUI

```
./build/pdnkit/pdnkit board.kicad_pcb
```

Opens the GUI, drops the board on the canvas. The dock panels on the
right configure each analysis:

- **Analysis** -- per-pad currents, source/sink, layer selection, run
  static IR drop. Toggle "Show current density" after solving to view
  |J| instead of voltage. Yellow ring marks the worst-drop hotspot.
- **Plane Z(f)** -- cavity model, port positions, decap table,
  auto-suggest, target-impedance overlay.
- **Transient** -- step current, dt, n_steps. Plot V(t) + max|V|.
- **Net Stats** -- per-net summary, click to load into the analysis
  panels.
- **Layers** -- visibility toggles.

Right-click two pads on the same net to probe effective resistance
(1 A injection, pop-up dialog with R in ohms / m-ohms).

## CLI

All analyses are also runnable headlessly:

### Static IR drop
```
pdnkit --analyze --net +3V3 --layer F.Cu --current 2.0 \
       --cell-size 0.3 board.kicad_pcb
```

### Cavity Z(f) sweep
```
pdnkit --zf --net +3V3 --layer F.Cu \
       --port1-x 5 --port1-y 5 --port2-x 15 --port2-y 5 \
       --f-min 1e6 --f-max 5e9 --points 300 \
       board.kicad_pcb
```

### Transient step response
```
pdnkit --transient --net +3V3 --layer F.Cu \
       --current 5.0 --dt 1e-9 --n-steps 5000 \
       board.kicad_pcb
```

### Effective resistance between two pads
```
pdnkit --probe-r --net +3V3 --layer F.Cu \
       --pad-a U1.1 --pad-b U2.1 board.kicad_pcb
```

### Via partial inductance
```
pdnkit --via-l --via-diameter 0.3 --via-length 1.6 --via-spacing 0.5
```
Self / mutual / loop inductance of a cylindrical via barrel (Grover/Ruehli closed form). Useful for sanity-checking decap mounting inductance or stitching-via arrays. Without `--via-spacing`, prints self-L only.
### Dielectric model (Djordjevic-Sarkar)
```
pdnkit --eps-f --frequency 1e9 --eps-inf 3.8 --delta-eps 1.0
```
Prints eps_r' / eps_r" / tan(delta) at one frequency under the causal Djordjevic-Sarkar fit. Defaults are a generic FR-4 (eps_inf=3.8, delta=1.0, f1=1 kHz, f2=1 GHz).

### IR drop with thermal coupling
```
pdnkit --thermal --net VRAIL --layer F.Cu --current 5.0 \
       --cell-size 0.5 --r-theta 100 board.kicad_pcb
```
Iterates the IR solve and copper resistivity until the steady-state temperature rise converges. Models the fact that high-current rails heat up, copper resistance climbs (alpha = 0.00393/C), and the drop is worse than the 20 C solve predicts. Default R_theta is 100 K/W (aggregate copper-to-ambient).

### VRM output impedance
```
pdnkit --vrm-z --vrm-r 5 --vrm-l 1 --vrm-f 1e6
```
R + j*omega*L model for a switching regulator. Useful to overlay on the cavity Z(f) plot and see where the VRM dominates vs the cap network vs the cavity itself.

### Target impedance from load spec
```
pdnkit --target-z --v-nom 0.9 --v-tol 0.03 --i-step 50
```
Computes the Larry Smith flat target `Z = V_nom * V_tol / I_step`. Use the result as the target line in the Plane Z(f) plot.

### Export SPICE netlist
```
pdnkit --spice --net +3V3 --layer F.Cu --current 2.0 \
       --cell-size 0.3 --out board.cir board.kicad_pcb
```
Dumps the IR-drop resistor network as a SPICE-3 netlist (R for each cell-to-cell resistor, I for current injectors at source pads, V_sink at sink pads). Load into ngspice or LTspice to co-simulate with a user-supplied IC load model, VRM circuit, or external decap bank. Omit `--out` to write to stdout.

### IPC-2152 power-aware DRC
```
pdnkit --drc --net +3V3 --drc-current 5.0 --drc-temp-rise 10 board.kicad_pcb
```
Flags every segment on `--net` whose width can't carry `--drc-current`
without exceeding `--drc-temp-rise` C above ambient, per the IPC-2221
closed form (which underlies IPC-2152's reference curves):

    I_max = k * dT^0.44 * A_mil^2 ^ 0.725

with k = 0.048 external (F.Cu / B.Cu), k = 0.024 internal. Each
violation reports actual width, required width, and layer position.

Exit codes:
- 0 -- success / no violations
- 1 -- bad arguments
- 2 -- parse failure
- 3 -- net not found
- 4 -- DRC violations present
- 5 -- mesher / solver failure (for analysis modes)

### Listing
```
pdnkit --list-nets   board.kicad_pcb
pdnkit --list-layers board.kicad_pcb
```

## Examples

See [examples/](examples/) for runnable demo scripts.

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for the internal design (mesher,
solver, cavity model, etc.).

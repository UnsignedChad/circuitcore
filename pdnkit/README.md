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

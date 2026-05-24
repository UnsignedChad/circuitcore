# pdnkit examples

Runnable shell scripts demonstrating each pdnkit headless analysis mode
against the bundled test fixture (`tests/fixtures/tiny_pdn.kicad_pcb`).

Build pdnkit first (`cmake --build build` from repo root), then run any
of the scripts from this directory.

| Script                  | What it runs                                              |
|-------------------------|-----------------------------------------------------------|
| `run_static_drop.sh`    | `--analyze` — static IR drop, prints `Vmax/Vmin/drop`     |
| `run_zf_sweep.sh`       | `--zf` — cavity-model Z(f) sweep, prints CSV to stdout    |
| `run_transient.sh`      | `--transient` — backward-Euler step response, CSV stdout  |
| `sweep_all_demos.py`    | Run `--analyze` on every KiCad demo (parser stress test)  |

All scripts assume the pdnkit binary is at `../build/pdnkit` and the
fixture at `../tests/fixtures/tiny_pdn.kicad_pcb` (run them from inside
this `examples/` directory). For a different board, edit the script or
pass the path as an argument where supported.

## Expected output (tiny_pdn fixture)

Each script's `expected/` text file shows the output you should see if
the build is healthy — useful for spot-checking after rebuilds or for
CI integration.

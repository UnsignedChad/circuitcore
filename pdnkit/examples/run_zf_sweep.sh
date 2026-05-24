#!/usr/bin/env bash
# Cavity-model Z(f) sweep on the tiny_pdn fixture.
# Writes CSV (freq_hz,abs_z_ohm) to stdout. Pipe to a file to plot:
#   ./run_zf_sweep.sh > zf.csv

set -euo pipefail
cd "$(dirname "$0")"

../build/pdnkit --zf \
    --net +3V3 \
    --layer F.Cu \
    --port1-x 5 --port1-y 5 \
    --port2-x 15 --port2-y 5 \
    --eps-r 4.3 --tan-delta 0.020 --thickness 1.6 \
    --f-min 1e6 --f-max 5e9 --points 200 --modes 30 \
    ../tests/fixtures/tiny_pdn.kicad_pcb

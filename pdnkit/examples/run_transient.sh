#!/usr/bin/env bash
# Step-response transient on the tiny_pdn fixture.
# 1 A step at t=0, 10 ns timestep, 200 steps -> 2 us simulation window.
# Writes CSV (time_s,v_obs_v,v_max_v) to stdout.

set -euo pipefail
cd "$(dirname "$0")"

../build/pdnkit --transient \
    --net +3V3 \
    --layer F.Cu \
    --current 1.0 \
    --cell-size 0.5 \
    --dt-ns 10 --n-steps 200 \
    --eps-r 4.3 --thickness 1.6 \
    ../tests/fixtures/tiny_pdn.kicad_pcb

#!/usr/bin/env bash
# Static IR drop on the tiny_pdn fixture.
# Expected (1.0 A on +3V3 / F.Cu with 0.5mm cells):
#   pdnkit IR drop  net=+3V3  layer=F.Cu  current=1.000A
#                   nodes=684  resistors=1312  Vmax=...mV  Vmin=0.000000mV
#                   drop=...mV

set -euo pipefail
cd "$(dirname "$0")"

../build/pdnkit --analyze \
    --net +3V3 \
    --layer F.Cu \
    --current 1.0 \
    --cell-size 0.5 \
    ../tests/fixtures/tiny_pdn.kicad_pcb

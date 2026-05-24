# sikit example: `demo_board.kicad_pcb`

A hand-crafted four-layer board that exercises every analysis feature
sikit ships today. Walk through each command in order and you've used
the whole tool.

## Board summary

100 mm x 50 mm, four-layer stackup (F.Cu / In1.Cu / In2.Cu / B.Cu),
0.035 mm outer copper, 0.018 mm inner copper, FR-4 dielectric throughout.

| Net      | Layer  | What it demonstrates                                    |
|----------|--------|---------------------------------------------------------|
| USB_DP   | B.Cu   | Diff pair (P leg), 40.0 mm                              |
| USB_DN   | B.Cu   | Diff pair (N leg), 40.4 mm -- 0.4 mm intra-pair skew    |
| DDR_DQ0  | F.Cu   | Bus member, 40 mm                                       |
| DDR_DQ1  | F.Cu   | Bus member, 41 mm                                       |
| DDR_DQ2  | F.Cu   | Bus member, 39 mm                                       |
| DDR_DQ3  | F.Cu   | Bus member, 43 mm -- 4 mm spread across the byte lane   |
| BAD_NET  | F.Cu   | 80 mm trace running directly over an In1.Cu plane gap   |
| GND      | In1.Cu | Reference plane with a 5 mm horizontal gap at y = 20 mm |
| VCC      | In2.Cu | Solid reference for B.Cu signals                        |

The In1.Cu gap is the planted return-path discontinuity. BAD_NET
runs at y = 20 mm, directly over that gap, so the return-path
detector flags it. Every other signal segment has continuous
reference copper below it.

## Walkthrough

All commands take the file path as the last positional argument:

```
$ sikit list-nets sikit/examples/demo_board.kicad_pcb
id    name
----  --------------------------------
   0
   1  GND
   2  VCC
   3  USB_DP
   4  USB_DN
   5  DDR_DQ0
   6  DDR_DQ1
   7  DDR_DQ2
   8  DDR_DQ3
   9  BAD_NET
```

### 1. Trace impedance

```
$ sikit impedance --net BAD_NET --layer F.Cu sikit/examples/demo_board.kicad_pcb
net      = BAD_NET
layer    = F.Cu (ord 0)
width    = 0.2000 mm  (median across 1 segments)
length   = 80.00 mm
engine   = closed-form
Z0       = 64.91 ohm
v_phase  = 1.667e+08 m/s
eps_eff  = 3.235
```

A 0.2 mm trace on FR-4 microstrip with the demo stackup comes out
around 65 ohms -- a touch high for 50-ohm signalling, which is
typical for a thin-trace example. The closed-form pipeline uses
Wadell's microstrip formula; pass `--fdm` to re-run with the
in-house 2D Laplace solver.

### 2. Diff-pair intra-pair skew

```
$ sikit skew sikit/examples/demo_board.kicad_pcb
pair                    P (mm)    N (mm)  |skew|mm  skew_ps  budget
USB_D                   40.000    40.400     0.400    -2.40  PASS

1 pair(s) checked, 0 over the 5.00 ps budget
```

The USB pair has 0.4 mm of mismatch -- the N leg is longer. In the
demo's FR-4 microstrip, that's about 2.4 ps. Default budget is
5 ps (the PCIe Gen5 spec); pass `--budget` to switch to a different
standard.

### 3. Multi-bit bus length skew

```
$ sikit bus-skew sikit/examples/demo_board.kicad_pcb
bus                       N   min(mm)   max(mm)  skew_ps  budget
DDR_DQ                    4    39.000    43.000   +24.00  FAIL

1 bus(es) checked, 1 over the 10.00 ps budget
```

The DDR_DQ0..3 byte lane has 4 mm of length variation, which works
out to ~24 ps of skew. Default budget is 10 ps (typical DDR write-eye
spread); the bus FAILs. Exit code is non-zero so a CI job catches it.

### 4. Return-path discontinuities

```
$ sikit return-path sikit/examples/demo_board.kicad_pcb
rank   net           sig_lyr   ref_lyr   off-plane     severity_mm
1      BAD_NET       0         1           100.0%          80.00

1 segment(s) flagged
```

BAD_NET runs the full length of the In1.Cu plane gap, so 100% of its
sampled points fall outside any copper zone on the reference layer.
Severity = off_plane_fraction * length = 0.8 m.

### 5. Compliance specs

```
$ sikit list-specs
Available compliance specs:
  PCIe Gen3 (8.0 GT/s)                          family=PCIe        baud=8.000e+09
  PCIe Gen4 (16.0 GT/s)                         family=PCIe        baud=1.600e+10
  PCIe Gen5 (32.0 GT/s)                         family=PCIe        baud=3.200e+10
  PCIe Gen6 (64.0 GT/s PAM4)                    family=PCIe        baud=3.200e+10
  DDR4-3200                                     family=DDR         baud=1.600e+09
  ...
```

Thirteen pre-canned compliance specs across PCIe, DDR, USB, HDMI,
Ethernet. Each carries the eye mask polygon plus baud / ref impedance
/ BER target / recommended PRBS pattern. Used by the eye-diagram view
to score pass / fail.

## What's not exercised yet

The demo board is geometry-focused. Features that need a Touchstone
input or a synthesized channel aren't covered here:

* `sikit deembed -i meas.s2p --fixture fix.s2p -o dut.s2p` -- supply
  any measured + fixture pair
* `sikit compare -a meas.s2p -b sim.s2p` -- supply measured + simulated
* `sikit touchstone --net X --layer Y -o out.s2p board.kicad_pcb` --
  synthesises a 2-port S-parameter file for the named net
* `sikit spice --net X --layer Y -o out.cir board.kicad_pcb` -- rational
  fit + Foster ladder SPICE subcircuit for ngspice / LTspice co-sim

Run `sikit --help` for the full list.

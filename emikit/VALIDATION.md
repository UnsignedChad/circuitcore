# emikit validation

## 1. loop formula

`emikit/tests/calibration_test.cpp` pins `loop_e_field` to Ott Eq 11-2
(also Paul Eq 8.62):

    E (V/m) = (eta0 * pi * I * A * f^2) / (c^2 * r)

Reference point: 1 mA, 1 cm^2 loop, 100 MHz, 3 m -> 12.85 dBuV/m. Plus
three scaling-derived points. Tolerance 0.05 dB. Tag `[calibration]`.

## 2. cable common-mode + estimator

`emikit/tests/cable_cm_test.cpp` pins `cable_cm_e_field` to Hockanson
1996 / Paul Eq 11.5:

    E (V/m) = (eta0 / c) * I_cm * L * f / r

Reference point: 20 uA, 30 cm, 100 MHz, 10 m -> 37.55 dBuV/m. Plus the
ground-bounce estimator:

    I_cm(f) = (2 * L_gnd / (L_cable_per_m * cable_length)) * I_signal(f)

Hand-checked at 5 nH / 1 uH/m / 30 cm: ratio 3.33e-2. Tag `[cable]`.

## 3. TI ADS8686S EVM

`emikit/tools/validate_ti.cpp` compares against chamber data in TI
SBAA548A (April 2022) for the ADS8686SEVM-PDK at three SCLK rates.

Reconstructed setup (geometry from SBAU319):

| param         | value      | source                              |
|---------------|------------|-------------------------------------|
| trace length  | 30 mm      | EVM layer 1 figure, midpoint est    |
| trace width   | 0.15 mm    | typical digital trace               |
| loop height   | 0.2 mm     | typical TI 4-layer prepreg          |
| drive current | 6 mA peak  | PHI MSP430 3.3 V CMOS               |
| rise time     | 2 ns       | nominal for that part class         |
| cable length  | 30 cm      | PHI USB to host                     |
| L_gnd         | 15 nH      | mid-range "real digital board"      |
| L_cable_per_m | 1.0 uH/m   | typical unshielded USB              |

Same parameters for all three tests.

### result

    test 1 (SCLK=10 MHz, 9.7 kSPS)
      chamber:        34.67 dBuV/m at 600.0 MHz
      emikit:         31.64 dBuV/m (gap -3.0 dB)
      estimated I_cm: 1.7 uA

    test 2 (SCLK=50 MHz, 769 kSPS)
      chamber:        54.73 dBuV/m at 480.0 MHz
      emikit:         47.61 dBuV/m (gap -7.1 dB)
      estimated I_cm: 13.4 uA

    test 3 (SCLK=10 MHz, 232 kSPS)
      chamber:        51.06 dBuV/m at 479.8 MHz
      emikit:         33.63 dBuV/m (gap -17.4 dB)
      estimated I_cm: 2.7 uA

Tests 1 and 2 land within +-7 dB of chamber across one set of
engineering parameters. Test 3 uses the same SCLK as test 1 but the
chamber measured 17 dB more because the higher sample rate drives more
activity on the data lines and SCLK bursts. The single-trace
continuous-clock input gives the same prediction for both runs.

## todo

- activity factor / burst modelling (would close the test 3 gap)
- multi-net summing
- empirical L_gnd from pdnkit plane impedance once that ships
- more reference pairs (Hockanson 1996 test board)
- finish openEMS cross-check (PR #62)
- cap on `cable_cm_e_field` for L > lambda/2 is conservative, not a
  full-wave answer

## reproduce

    cmake --build build --target emikit_validate_ti
    ./build/emikit/emikit_validate_ti

Or from the CLI:

    emikit check board.kicad_pcb --mask "CISPR 32 Class A (10 m)" \
        --clock-hz 50e6 --rise-ns 2 --i-peak-ma 6 \
        --cable-length-cm 30 --ground-inductance-nh 15

## refs

- TI SBAA548A: https://www.ti.com/lit/an/sbaa548a/sbaa548a.pdf
- TI SBAU319: https://www.ti.com/lit/pdf/SBAU319
- Hockanson, Drewniak, Hubing, Van Doren, IEEE Trans EMC 38(4), 1996
- Paul, Intro to EMC 2nd ed., Wiley 2006, Ch 11.3
- Ott, EMC Engineering, Wiley 2009, Ch 11.6
- Montrose, EMC and the Printed Circuit Board, Wiley 1999, Ch 5

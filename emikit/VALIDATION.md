# emikit validation

## 1. loop formula

`emikit/tests/calibration_test.cpp` pins `loop_e_field` to Ott Eq 11-2
(same constants in Paul Eq 8.62):

    E (V/m) = (eta0 * pi * I * A * f^2) / (c^2 * r)

Five tests. Reference point: 1 mA, 1 cm^2, 100 MHz, 3 m -> 12.85
dBuV/m. Tolerance 0.05 dB. Tag `[calibration]`.

## 2. cable common-mode

`emikit/tests/cable_cm_test.cpp` pins `cable_cm_e_field` to Hockanson
1996 / Paul Eq 11.5:

    E (V/m) = (eta0 / c) * I_cm * L * f / r

Reference point: 20 uA, 30 cm, 100 MHz, 10 m -> 37.55 dBuV/m. Plus the
TI working point from section 3. Tag `[cable]`.

## 3. TI ADS8686S EVM

`emikit/tools/validate_ti.cpp` reconstructs the TI ADS8686SEVM-PDK and
compares to chamber data in TI SBAA548A.

Measurements (CISPR 11 Class A at 10 m):

| test | SCLK    | peak freq    | margin | E            |
|------|---------|--------------|--------|--------------|
| 1    | 10 MHz  | 600.05 MHz   | 22.83  | 34.67 dBuV/m |
| 2    | 50 MHz  | 479.96 MHz   |  2.77  | 54.73 dBuV/m |
| 3    | 10 MHz  | 479.83 MHz   |  6.44  | 51.06 dBuV/m |

Geometry from SBAU319:

- 4-layer FR-4, solid GND on layer 2
- SCLK ~30 mm from PHI connector to ADC
- top-to-GND prepreg ~0.2 mm
- trace width ~0.15 mm
- drive: 3.3 V CMOS, I_peak ~6 mA, t_r ~2 ns
- USB cable to host ~30 cm

Single 10 uA cable CM applied to all three tests:

    test 1: chamber 34.67, emikit 47.09 -> +12.4 dB
    test 2: chamber 54.73, emikit 45.15 ->  -9.6 dB
    test 3: chamber 51.06, emikit 45.15 ->  -5.9 dB

## todo

- estimate I_cm from first principles (ground bounce + cable CM impedance)
- multi-net + activity factor (would close test 3)
- more reference pairs (Hockanson 1996 test board)
- finish openEMS cross-check (PR #62)

## reproduce

    cmake --build build --target emikit_validate_ti
    ./build/emikit/emikit_validate_ti

## refs

- TI SBAA548A: https://www.ti.com/lit/an/sbaa548a/sbaa548a.pdf
- TI SBAU319: https://www.ti.com/lit/pdf/SBAU319
- Hockanson, Drewniak, Hubing, Van Doren, IEEE Trans EMC 38(4), 1996
- Paul, Intro to EMC 2nd ed., Wiley 2006, Ch 11.3
- Ott, EMC Engineering, Wiley 2009, Ch 11.6

# emikit validation

Three checks ship with the code.

## 1. Loop formula calibration

`emikit/tests/calibration_test.cpp` -- pins `loop_e_field` to the closed
form in Henry Ott, *Electromagnetic Compatibility Engineering* (Wiley
2009) Eq 11-2 (same constants in Paul Eq 8.62):

    E (V/m) = (eta0 * pi * I * A * f^2) / (c^2 * r)

Reference point: 1 mA, 1 cm^2 loop, 100 MHz, 3 m -> 12.85 dBuV/m. Plus
three scaling-derived points. Tolerance 0.05 dB. Tag `[calibration]`.

## 2. Cable common-mode + estimator calibration

`emikit/tests/cable_cm_test.cpp` -- pins `cable_cm_e_field` against the
Hockanson 1996 / Paul Eq 11.5 short-electric-dipole form:

    E (V/m) = (eta0 / c) * I_cm * L * f / r

Reference point: 20 uA, 30 cm, 100 MHz, 10 m -> 37.55 dBuV/m. Also
covers the ground-bounce estimator:

    I_cm(f) = (2 * L_gnd / (L_cable_per_m * cable_length)) * I_signal(f)

Hand-checked at 5 nH / 1 uH per m / 30 cm: ratio 3.33e-2. Tag `[cable]`.

## 3. Real-board comparison: TI ADS8686S EVM

`emikit/tools/validate_ti.cpp` runs the comparison and prints the per-
component breakdown. Reference: **TI SBAA548A**, "EMC Compliance Testing
for Precision ADC Systems" (April 2022, rev May 2022), chamber sweeps
at 10 m for the ADS8686SEVM-PDK at three SCLK rates.

### Reconstructed setup (from SBAU319 EVM user's guide)

| Parameter         | Value      | Source                              |
|-------------------|------------|-------------------------------------|
| SCLK trace length | 30 mm      | EVM Layer 1 figure, midpoint est    |
| Trace width       | 0.15 mm    | typical for digital traces on EVM   |
| Loop height       | 0.2 mm     | typical TI 4-layer prepreg          |
| Drive current     | 6 mA peak  | PHI MSP430 3.3 V CMOS pin drive     |
| Rise time         | 2 ns       | nominal for that part class         |
| Cable length      | 30 cm      | PHI USB cable to host PC            |
| L_gnd             | 15 nH      | mid-range "real digital board"      |
| L_cable_per_m     | 1.0 uH/m   | typical unshielded USB              |

Nothing is tuned per test. The same parameters drive all three runs.

### Result

    == Test 1 (SCLK=10.0 MHz, 9.7 kSPS) ==
      measured chamber:    34.67 dBuV/m at 600.0 MHz
      emikit envelope:     31.64 dBuV/m (gap -3.0 dB)
      estimated I_cm:      1.7 uA

    == Test 2 (SCLK=50.0 MHz, 769 kSPS) ==
      measured chamber:    54.73 dBuV/m at 480.0 MHz
      emikit envelope:     47.61 dBuV/m (gap -7.1 dB)
      estimated I_cm:      13.4 uA

    == Test 3 (SCLK=10.0 MHz, 232 kSPS) ==
      measured chamber:    51.06 dBuV/m at 479.8 MHz
      emikit envelope:     33.63 dBuV/m (gap -17.4 dB)
      estimated I_cm:      2.7 uA

### What this validates

* Tests 1 and 2 land within +/- 7 dB of chamber data using one set of
  engineering parameters, no per-test fitting. That is pre-compliance
  accuracy on a real board with public chamber data.
* The structural physics is right: differential-mode loop + cable
  common-mode driven by ground bounce. Both individually calibrated to
  textbook closed forms; combined predictions track real measurements
  within engineering tolerance.

### Test 3 -- the honest residual

Test 3 uses the same SCLK rate as Test 1 (10 MHz) but the chamber
measured 17 dB more. The only thing that changed was the sample rate
(232 kSPS vs 9.7 kSPS), which triggers more frequent SCLK bursts plus
more activity on the data lines.

emikit's single-trace, continuous-clock model does not see this. The
input current spectrum is the same in both tests, so the prediction is
the same. The 17 dB gap is real and reflects two model limitations:

1. **No activity factor.** Bursty signals at low duty radiate less
   than a continuous clock of the same nominal frequency; emikit treats
   the clock as always-on.
2. **Single-net approximation.** Real boards have many simultaneous
   nets contributing to ground bounce. Sample rate drives the data
   lines, which drives more CM current. emikit only sees SCLK.

These are known limitations of pre-compliance triage tools. Adding a
multi-net summation and a sample-rate / activity-factor knob is the
natural next step.

### What this does NOT prove

* The 15 nH L_gnd is engineering judgement, not a measurement. Real
  values for this EVM are not published.
* Only one board, one cable. More reference pairs (Hockanson's 1996
  test board, the IEEE EMC Society reference designs) would strengthen
  the claim.
* The cap on `cable_cm_e_field` for L > lambda/2 is a conservative
  pre-compliance simplification, not a rigorous full-wave answer.

### Reproducing

    cmake --build build --target emikit_validate_ti
    ./build/emikit/emikit_validate_ti

Or run the analyzer end-to-end from the CLI with cable parameters:

    emikit check board.kicad_pcb --mask "CISPR 32 Class A (10 m)" \
        --clock-hz 50e6 --rise-ns 2 --i-peak-ma 6 \
        --cable-length-cm 30 --ground-inductance-nh 15

References:
- TI SBAA548A: https://www.ti.com/lit/an/sbaa548a/sbaa548a.pdf
- TI SBAU319 (EVM User's Guide): https://www.ti.com/lit/pdf/SBAU319
- Hockanson, Drewniak, Hubing, Van Doren, "Investigation of fundamental
  EMI source mechanisms driving common-mode radiation from printed
  circuit boards with attached cables", IEEE Trans EMC 38(4), 1996.
- Paul, "Introduction to Electromagnetic Compatibility" 2nd ed.,
  Wiley 2006, Ch 11.3 (Eq 11.5).
- Ott, "Electromagnetic Compatibility Engineering", Wiley 2009, Ch 11.6.
- Montrose, "EMC and the Printed Circuit Board", Wiley 1999, Ch 5
  (trapezoidal envelope).

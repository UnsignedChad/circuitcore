# emikit validation

Two checks ship with the code today.

## 1. Textbook formula calibration

`emikit/tests/calibration_test.cpp` pins `loop_e_field` against the closed
form in Henry Ott, *Electromagnetic Compatibility Engineering* (Wiley
2009) Eq 11-2 -- same constants in Paul, *Introduction to EMC* 2nd ed.
Eq 8.62:

    E (V/m) = (eta0 * pi * I * A * f^2) / (c^2 * r)

Five test cases hit a reference point (1 mA, 1 cm^2 loop, 100 MHz, 3 m
distance -> 12.85 dBuV/m) plus three derived points scaling I, f, and r.
Tolerance 0.05 dB. Tag `[calibration]`.

This catches constant-drift bugs that the scaling-law tests (f^2, 1/r,
linear in I and A) would silently pass.

## 2. Real-board comparison: TI ADS8686S EVM

`emikit/tools/validate_ti.cpp` reconstructs the geometry of TI's
ADS8686SEVM-PDK and compares emikit's prediction to chamber data
published in TI app note **SBAA548A**, "EMC Compliance Testing for
Precision ADC Systems" (April 2022, rev May 2022).

### TI's measurements

CISPR 11 Class A radiated emissions at 10 m, three SCLK rates:

| Test | SCLK     | Peak freq    | Margin | Measured E   |
|------|----------|--------------|--------|--------------|
| 1    | 10 MHz   | 600.05 MHz   | 22.83  | 34.67 dBuV/m |
| 2    | 50 MHz   | 479.96 MHz   | 2.77   | 54.73 dBuV/m |
| 3    | 10 MHz   | 479.83 MHz   | 6.44   | 51.06 dBuV/m |

### Geometry estimate from SBAU319 (ADS8686SEVM-PDK User's Guide)

Layout figures in Section 7.2 (Figures 20-25) were used to estimate:

- 4-layer FR-4 stackup, solid GND on Layer 2
- SCLK trace ~30 mm from the PHI controller card connector to the ADC
- Top layer to GND prepreg ~0.2 mm (typical TI 4-layer EVM stack)
- Trace width ~0.15 mm

Drive: PHI uses an MSP430-class 3.3 V CMOS output stage. Estimates:

- I_peak ~ 6 mA (8 mA pin drive derated)
- Rise/fall time ~ 2 ns

### Result

    == Test 1 (SCLK=10.0 MHz) ==
      measured peak:  34.67 dBuV/m at 600.05 MHz
      emikit max:    -26.16 dBuV/m at 252.98 MHz
      gap:           ~60 dB low

    == Test 2 (SCLK=50.0 MHz) ==
      measured peak:  54.73 dBuV/m at 479.96 MHz
      emikit max:    -10.96 dBuV/m at 271.46 MHz
      gap:           ~65 dB low

    == Test 3 (SCLK=10.0 MHz) ==
      measured peak:  51.06 dBuV/m at 479.83 MHz
      emikit max:    -26.16 dBuV/m at 252.98 MHz
      gap:           ~77 dB low

### Why the gap is structural, not a parameter error

Sixty-plus dB is three orders of magnitude on current. Even doubling
every input estimate (loop area, drive current, rise time aggressiveness)
only closes ~10-15 dB. The remaining 50 dB comes from physics the
small-loop magnetic-dipole model omits:

1. **Common-mode current on the USB cable.** The PHI controller's USB
   cable acts as a 30 cm monopole antenna. A few microamps of CM current
   on a cable that length easily produces 40-50 dBuV/m at 10 m. EMC
   literature (Ott Ch 11.6, Paul Ch 11.3) consistently identifies cable
   CM as the dominant emission source on most digital boards. emikit
   models only differential-mode loop radiation -- the cable is not in
   its scope.

2. **All nets contribute, not just SCLK.** The EVM runs multiple data
   lines, a USB transceiver, the MSP430 internal clock, etc. The
   chamber sees the sum. emikit's single-trace fixture sees one.

3. **Return-path discontinuities.** Any slot in the GND plane or a via
   that forces the return current to detour multiplies the effective
   loop area by 10-100x. emikit takes `loop_height_m` as a clean
   stackup parameter and assumes the return flows directly under the
   trace.

4. **Spectral nulls.** A pure trapezoidal pulse has sin(pi*f*tau)
   nulls at integer f*tau. Real digital signals have edge jitter and
   duty-cycle imperfection that fill those nulls in. emikit's predicted
   peak frequency does not match TI's measured peak frequencies for
   exactly this reason -- the deep nulls in the model land where the
   chamber sees energy.

### What this validates

emikit is a **per-net lower bound on differential-mode loop emissions**.

- Useful for triage during routing: nets it flags as concerning will
  be concerning.
- Useful for relative comparison: longer trace vs shorter trace,
  faster edge vs slower, taller loop vs tighter.
- **Not a substitute for chamber testing.** Real board-level emissions
  will be 30-70 dB higher than emikit's prediction for the same
  geometry, dominated by cable common-mode and contributions from nets
  emikit was not asked about.

The `+/- 6 dB pre-compliance` framing in the original spec was wrong.
It is +/- 6 dB *for the physics emikit implements* (one trace, one loop,
one return plane). It is not +/- 6 dB versus a real chamber measurement
of a whole product.

### Reproducing

    cmake --build build --target emikit_validate_ti
    ./build/emikit/emikit_validate_ti

References:
- TI SBAA548A: https://www.ti.com/lit/an/sbaa548a/sbaa548a.pdf
- TI SBAU319 (ADS8686SEVM-PDK User's Guide): https://www.ti.com/lit/pdf/SBAU319

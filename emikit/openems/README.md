# openEMS cross-check scripts

Full-wave FDTD against emikit's closed form on the same single-trace
fixture. If the two agree to a few dB on the geometry emikit models,
the chamber-vs-emikit gap on the TI EVM is scope (multi-net, activity
factor, cable CM), not math.

## status

scripts run, openEMS builds and produces port + near-field data, but
the broadband Gaussian + -40 dB auto-terminate stops at ~2 ns -- too
short for a clean 30 MHz spectrum. needs more iteration before the
numbers are usable.

files:

* `openems_microstrip.py` -- 30 mm trace, 0.15 mm wide, 0.2 mm above
  solid GND, lumped ports + NF2FF at 10 m
* `compare_openems_emikit.py` -- runs emikit's `loop_e_field` over the
  FDTD-measured I_port spectrum

run on a workstation with openEMS Python bindings:

    source ~/opt/openEMS/venv/bin/activate
    python3 openems_microstrip.py
    python3 compare_openems_emikit.py

## todo

- bump `NrTS`, disable `EndCriteria`, or run CW per frequency instead
  of broadband Gaussian
- try `AddMSLPort` instead of `AddLumpedPort` (less sensitivity near
  the port terminals)
- mesh capped under 2M cells; tighter spacing blows past 30 GB

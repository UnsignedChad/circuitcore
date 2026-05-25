# OpenEMS cross-check scripts

Apples-to-apples full-wave FDTD comparison against emikit's closed
form for the **same** single-trace fixture.  If emikit and OpenEMS
agree to a few dB on the geometry emikit models, the algorithm is
right and the chamber-vs-emikit gap on the TI EVM is purely scope
(multi-net, activity factor, cable common-mode).

## Status: infrastructure ready, FDTD parameters need iteration

* `openems_microstrip.py` -- builds the fixture (30 mm trace, 0.15 mm
  wide, 0.2 mm above solid GND, lumped ports + NF2FF at 10 m)
* `compare_openems_emikit.py` -- consumes the FDTD output and runs
  emikit's `loop_e_field` for the same I_port spectrum

Run on a workstation with OpenEMS Python bindings installed:

    source ~/opt/openEMS/venv/bin/activate
    python3 openems_microstrip.py     # produces full_result.h5
    python3 compare_openems_emikit.py

## Known issues to fix before getting useful numbers

1. The Gaussian excitation auto-terminates via the -40 dB energy
   criterion at ~2 ns, giving only ~17 port samples over the run.
   That is below the 33 ns needed to resolve 30 MHz cleanly. Either:
   - bump `NrTS` past the natural decay and disable EndCriteria, or
   - run multiple CW simulations at fixed frequencies (more robust).

2. Lumped port has shown numerical sensitivity to mesh near the port
   terminals. Switching to `AddMSLPort` (microstrip-line port,
   designed for this geometry) is worth trying first.

3. Mesh budget kept under 2M cells with explicit fine lines near the
   trace + max_res=2.0 mm out to absorbing boundaries. Going finer
   blows memory past 30 GB.

## Why this is still worth committing

Even without final numbers, the scripts capture the methodology so the
experiment is reproducible by anyone with OpenEMS installed. The
closed-form algorithm has already been pinned to its textbook source
(Ott Eq 11-2, Hockanson 1996) at 0.05 dB; what an FDTD cross-check
would prove is that those textbook formulas are themselves a faithful
reduction of the full Maxwell problem at this scale. That is a known
result in the EMC literature -- the experiment is confirmation, not
the foundation of the validation chain.

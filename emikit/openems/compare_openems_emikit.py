#!/usr/bin/env python3
"""feeds openEMS's measured port-current spectrum into emikit's
loop_e_field and compares to the full-wave far-field."""
import math
import h5py
import numpy as np

ETA0 = 376.730313668
C0   = 2.99792458e8

# Geometry (must match openems_run.py).
TRACE_LEN_M    = 30.0e-3
LOOP_HEIGHT_M  = 0.2e-3
OBS_DIST_M     = 10.0
LOOP_AREA      = TRACE_LEN_M * LOOP_HEIGHT_M


def emikit_loop_e_field(i_a, freq_hz, distance_m, area_m2):
    """Ott Eq 11-2 -- same as emikit's LoopEmissions::loop_e_field."""
    return (ETA0 * math.pi * i_a * area_m2 * freq_hz ** 2) / (C0 ** 2 * distance_m)


def main():
    with h5py.File("/home/chad/openems-runs/emikit_microstrip_air_result.h5",
                   "r") as h:
        freqs   = h["freq_hz"][...]
        i_port  = h["i_port_a"][...]
        e_full  = h["e_v_m"][...]
        e_full_dbuv = h["e_dbuv_m"][...]

    print(f"{'freq_MHz':>9} {'|I|mA':>8} {'E_FW_dBuV':>11} "
          f"{'E_emikit_dBuV':>14} {'delta_dB':>10}")
    print("-" * 60)

    e_emikit_all = []
    deltas = []
    for f, ip, efw_v, efw_db in zip(freqs, i_port, e_full, e_full_dbuv):
        e_emikit_v = emikit_loop_e_field(abs(ip), f, OBS_DIST_M, LOOP_AREA)
        if e_emikit_v <= 0:
            e_emikit_db = -1000.0
        else:
            e_emikit_db = 20.0 * math.log10(e_emikit_v * 1e6)
        e_emikit_all.append(e_emikit_db)
        delta = e_emikit_db - efw_db
        deltas.append(delta)
        print(f"{f / 1e6:9.1f} {abs(ip) * 1e3:8.4f} {efw_db:11.2f} "
              f"{e_emikit_db:14.2f} {delta:+10.2f}")

    valid = [d for d in deltas if abs(d) < 200]
    if valid:
        print(f"\nmean |delta| = {np.mean(np.abs(valid)):.2f} dB")
        print(f"max  |delta| = {np.max (np.abs(valid)):.2f} dB")
        print(f"rms       = {np.sqrt(np.mean(np.array(valid) ** 2)):.2f} dB")


if __name__ == "__main__":
    main()

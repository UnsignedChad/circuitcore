#!/usr/bin/env python3
"""Full-fixture FDTD with sane mesh grading."""
import os
import numpy as np
import h5py
from openEMS import openEMS
from CSXCAD import ContinuousStructure

sim = os.path.expanduser("~/openems-runs/full")
os.makedirs(sim, exist_ok=True)

TRACE_LEN = 30.0
TRACE_W   = 0.15
TRACE_T   = 0.035
SUB_H     = 0.2
GND_HALF  = 30.0
AIR       = 80.0

FDTD = openEMS(EndCriteria=1e-4, NrTS=60000)
FDTD.SetGaussExcite(500e6, 500e6)
FDTD.SetBoundaryCond(['MUR'] * 6)
CSX = ContinuousStructure()
FDTD.SetCSX(CSX)
CSX.GetGrid().SetDeltaUnit(1e-3)

gnd = CSX.AddMetal("gnd")
gnd.AddBox([-GND_HALF, -GND_HALF, 0.0], [GND_HALF, GND_HALF, 0.0])

trace = CSX.AddMetal("trace")
trace.AddBox([-TRACE_LEN / 2, -TRACE_W / 2, SUB_H],
             [ TRACE_LEN / 2,  TRACE_W / 2, SUB_H + TRACE_T])

FDTD.AddLumpedPort(1, 50.0,
                   [-TRACE_LEN / 2, -TRACE_W / 2, 0.0],
                   [-TRACE_LEN / 2,  TRACE_W / 2, SUB_H],
                   'z', excite=True)
FDTD.AddLumpedPort(2, 50.0,
                   [ TRACE_LEN / 2, -TRACE_W / 2, 0.0],
                   [ TRACE_LEN / 2,  TRACE_W / 2, SUB_H],
                   'z', excite=False)

# Explicit mesh lines near the trace + coarse at boundary, then smooth
# with a generous max_res (2 mm) so the mesh GROWS away from the fine
# lines instead of being held at 0.05 mm everywhere.
mesh = CSX.GetGrid()

# X: coarse end-of-domain + fine ends-of-trace + middle filler.
x_lines = [-GND_HALF - AIR, -GND_HALF, -TRACE_LEN/2 - 1.0,
           -TRACE_LEN/2 - 0.2, -TRACE_LEN/2,
           -TRACE_LEN/4, 0.0, TRACE_LEN/4,
           TRACE_LEN/2, TRACE_LEN/2 + 0.2,
           TRACE_LEN/2 + 1.0, GND_HALF, GND_HALF + AIR]
mesh.AddLine('x', x_lines)
mesh.SmoothMeshLines('x', 2.0, ratio=1.4)

# Y: tight cluster around trace, then coarse out.
y_lines = [-GND_HALF - AIR, -GND_HALF, -2.0, -0.5, -TRACE_W,
           -TRACE_W/2, -TRACE_W/4, 0.0, TRACE_W/4, TRACE_W/2,
           TRACE_W, 0.5, 2.0, GND_HALF, GND_HALF + AIR]
mesh.AddLine('y', y_lines)
mesh.SmoothMeshLines('y', 2.0, ratio=1.4)

# Z: fine through dielectric+trace, coarse out.
z_lines = [-AIR, -10.0, -1.0, 0.0, SUB_H/4, SUB_H/2, 3*SUB_H/4,
           SUB_H, SUB_H + TRACE_T/2, SUB_H + TRACE_T,
           SUB_H + 0.2, 1.0, 10.0, SUB_H + AIR]
mesh.AddLine('z', z_lines)
mesh.SmoothMeshLines('z', 2.0, ratio=1.4)

nx, ny, nz = (len(mesh.GetLines(a)) for a in 'xyz')
cells = nx * ny * nz
print(f"Mesh: {nx} x {ny} x {nz} = {cells:,} cells", flush=True)
assert cells < 30_000_000, f"mesh too big ({cells:,}), tune ratios"

nf2ff = FDTD.CreateNF2FFBox()

FDTD.Run(sim, verbose=2, cleanup=False)

freqs = np.linspace(30e6, 1.0e9, 40)
ff = nf2ff.CalcNF2FF(sim, freqs, np.array([90.0]), np.array([0.0]),
                     radius=10.0)
e_ff = np.abs(ff.E_norm[0])[:, 0, 0]

ut = np.loadtxt(os.path.join(sim, 'port_ut_1'))
it = np.loadtxt(os.path.join(sim, 'port_it_1'))
dt = ut[1, 0] - ut[0, 0]
t = ut[:, 0]
i_t = it[:, 1]
i_f = np.array([np.sum(i_t * np.exp(-2j * np.pi * f * t)) * dt
                 for f in freqs])
i_port = np.abs(i_f)

out = os.path.expanduser("~/openems-runs/full_result.h5")
with h5py.File(out, "w") as h:
    h.create_dataset("freq_hz",  data=freqs)
    h.create_dataset("i_port_a", data=i_port)
    h.create_dataset("e_v_m",    data=e_ff)
    h.create_dataset("e_dbuv_m", data=20.0 * np.log10(e_ff * 1e6))

print(f"\n{'freq_MHz':>9} {'|I|mA':>9} {'E_dBuV/m':>10}", flush=True)
for k in range(len(freqs)):
    print(f"{freqs[k] / 1e6:9.1f} {i_port[k] * 1e3:9.4f} "
          f"{20.0 * np.log10(e_ff[k] * 1e6):10.2f}", flush=True)
print(f"\nwrote {out}", flush=True)

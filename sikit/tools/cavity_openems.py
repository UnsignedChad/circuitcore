#!/usr/bin/env python3
"""Cross-validation reference: PEC cavity TE_101 mode via openEMS.

Geometry: 50 x 30 x 20 mm rectangular PEC box, dx = 2 mm.
Analytic TE_101 frequency (Pozar):
    f = c/2 * sqrt((1/a)^2 + (1/d)^2)
with a=50mm, d=20mm  ->  ~8.07 GHz.

Excite Ez at an off-centre point, probe Ez at another asymmetric
point, FFT the probe time series, find the dominant peak in the
TE_101 band.

Run:
  /home/chad/opt/openEMS/venv/bin/python cavity_openems.py
Writes <out>/openems_te101_peak.txt with the peak frequency.
"""
import os, sys, tempfile
import numpy as np

from CSXCAD import ContinuousStructure
from openEMS import openEMS
from openEMS.physical_constants import C0

a_mm, b_mm, d_mm = 50.0, 30.0, 20.0
unit = 1e-3

f_center = 8.0e9
f_bw     = 4.0e9

FDTD = openEMS(NrTS=8000, EndCriteria=1e-4)
FDTD.SetGaussExcite(f_center, f_bw)
FDTD.SetBoundaryCond(['PEC']*6)

CSX = ContinuousStructure()
FDTD.SetCSX(CSX)
mesh = CSX.GetGrid()
mesh.SetDeltaUnit(unit)
mesh.AddLine('x', np.linspace(0, a_mm, 26))   # 25 cells
mesh.AddLine('y', np.linspace(0, b_mm, 16))   # 15 cells
mesh.AddLine('z', np.linspace(0, d_mm, 11))   # 10 cells

# Ez excitation: small 3D box one cell on each side. exc_type=0 (soft E).
dx = 2.0  # in mm (mesh delta unit is 1e-3 m)
sx, sy, sz = 0.40*a_mm, 0.50*b_mm, 0.30*d_mm
exc = CSX.AddExcitation('src_ez', exc_type=0, exc_val=[0, 0, 1])
exc.AddBox([sx, sy, sz], [sx + dx, sy + dx, sz + dx])

# Voltage-style probe (line integral of Ez over one cell in z).
px, py, pz = 0.70*a_mm, 0.50*b_mm, 0.70*d_mm
probe = CSX.AddProbe('ez_probe', p_type=0, weight=-1.0)
probe.AddBox([px, py, pz], [px, py, pz + dx])

Sim_Path = '/tmp/openems_cavity'
os.makedirs(Sim_Path, exist_ok=True)
FDTD.Run(Sim_Path, verbose=2, cleanup=True)

# The probe writes 'ez_probe' as a text file in Sim_Path.
fn = os.path.join(Sim_Path, 'ez_probe')
data = np.loadtxt(fn, comments='%')
t = data[:, 0]
v = data[:, 1]
dt = t[1] - t[0]
print(f"probe samples: {len(t)}  dt = {dt:.3e} s")

# FFT and find the peak in the (5e9, 12e9) band -- TE_101 expected ~8 GHz.
N = 1
while N < len(v):
    N *= 2
padded = np.zeros(N)
padded[:len(v)] = v
V = np.fft.rfft(padded)
freqs = np.fft.rfftfreq(N, d=dt)

mask = (freqs > 5e9) & (freqs < 12e9)
peak_idx = np.argmax(np.abs(V[mask]))
peak_freq = freqs[mask][peak_idx]
peak_amp  = np.abs(V[mask][peak_idx])

print(f"openEMS TE_101 peak: {peak_freq/1e9:.4f} GHz  (|V| = {peak_amp:.3e})")

out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          'openems_te101_peak.txt')
with open(out_path, 'w') as f:
    f.write(f"# openEMS PEC cavity TE_101 peak\n")
    f.write(f"# cavity (mm): {a_mm} x {b_mm} x {d_mm}\n")
    f.write(f"# dx (mm)    : 2.0\n")
    f.write(f"peak_freq_hz {peak_freq:.6e}\n")
    f.write(f"peak_amp     {peak_amp:.6e}\n")
print(f"wrote {out_path}")

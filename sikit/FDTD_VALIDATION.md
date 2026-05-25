# sikit FDTD3D validation

Five checks ship with the full-wave solver. All run in
`sikit/tests/sikit_tests` under tags `[fdtd3d]`, `[fdtd-raster]`,
`[fdtd-port]`. 27 cases / 118 assertions, all green on `main`.

## 1. CFL bound matches the closed-form

`fdtd3d_test.cpp` -- pins `cfl_dt(g)` against

    dt <= safety / (c * sqrt(1/dx^2 + 1/dy^2 + 1/dz^2))

from Taflove & Hagness, *Computational Electrodynamics* ch. 4. Tag
`[fdtd3d]`. Tolerance 1e-12.

## 2. Causality: front cannot outrun c

A step Ez source at the centre of a vacuum grid; the probe 10 cells
away in +x stays **exactly** 0.0 until the wave-front can have reached
it, then is nonzero thereafter. With safety 0.99 and isotropic dx, the
front advances 0.99/sqrt(3) ≈ 0.57 cells per step, so 10 cells take at
least 18 steps; we require `probe[n] == 0` for `n < 13`. Inside an
epsr=4 block the same probe stays `<1%` of the vacuum amplitude at
step 25 -- direct confirmation of c/sqrt(4) wave speed.

## 3. Mur ABC absorbs the wave

Same Gaussian-pulse-and-volume-integral setup run twice: PEC walls and
Mur 1st-order. After 220 steps the Mur box's `sum(Ez^2)` is `<20%` of
the PEC box's -- the absorber has shed most of the radiated energy
through the walls. Tag `[fdtd3d][mur]`.

## 4. Conductivity dissipates energy

Pulse-excite a closed PEC box filled with sigma=5 S/m vs sigma=0;
volumetric `sum(Ez^2)` after the source goes silent is `<60%` of the
lossless run. Confirms the Yee-Hagness Ca/Cb loss formulation (Taflove
eq. 3.32) actually damps, and that the vacuum collapse to (Ca=1,
Cb=dt/eps0) is correct (defaults-vacuum test pins that exactly).

## 5. Port + S-parameter pipeline

`fdtd_port_test.cpp` -- two end-to-end checks:

- **Well-matched vacuum:** two identical runs of a soft-source port
  in a Mur-bounded vacuum produce reflected = total - incident = 0.
  `extract_s11_from_histories` returns exactly `0.0+0.0i` at every
  requested frequency. Pins the FFT + bin-interpolation arithmetic.
- **PEC scatterer:** a 1-cell PEC slab in the dipole's broadside lobe
  raises `|S11|` above the FFT round-off floor by orders of magnitude
  (`> 1e-5` vs the round-off floor of a few times 1e-12 from
  identical-run subtraction).

## What's NOT in v1

Three known follow-ups, called out in the module headers so they
don't get rediscovered:

- **CPML** for grazing-incidence absorption -- Mur 1st-order is fine
  for axial port-fed runs but loses absorption at grazing angles. The
  Roden & Gedney (2000) auxiliary-psi-field upgrade is a v2.
- **TF/SF source** for clean broadband port excitation -- the current
  soft source radiates from a point, which means the incident
  reference run includes the dipole's near-field. Acceptable for the
  v1 pipeline demo; not for accurate microstrip S21.
- **High-Q microstrip extraction** -- the rasteriser is staircased to
  the Yee grid, and the port is a single Yee cell rather than a true
  reference-plane integration. Real S21 across a routed trace needs
  both upgrades above.

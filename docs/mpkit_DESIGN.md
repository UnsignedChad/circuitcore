# mpkit -- multiphysics solver for circuitcore

## Why this kit exists

sikit, pdnkit and emikit each cover one physics domain in isolation
(signal integrity, power integrity, EMI). The questions PCB engineers
actually face on real boards are coupled:

- A power rail with 80 A through wide planes dissipates Joule heat
  that warps the board, shifts trace impedances and re-derates the
  copper. That is IR drop -> temperature -> mechanical -> impedance.
- A high-current switching converter heats local FR-4, shifts the
  dielectric constant and detunes a nearby filter. That is power ->
  temperature -> dielectric -> S-parameters.
- Repeated power cycling fatigues solder joints. That is transient
  thermal -> mechanical cycling -> reliability.

mpkit is the tab that orchestrates the other kits and the new solvers
needed to close those loops. The design goal is COMSOL-style usability:
one model tree, one 3D viewer, drag a physics on top of another to
couple them, sweep over any input, animate the result over time.

## What mpkit owns vs delegates

mpkit DOES NOT reimplement physics that already lives in another kit.
It calls the existing public APIs and exposes the result as one of its
fields. Concretely:

| Physics                       | Owned by      | mpkit usage                                  |
|-------------------------------|---------------|----------------------------------------------|
| Static IR drop                | pdnkit        | call IrSolver::solve, voxel-rasterize        |
| Transient IR + cavity decap   | pdnkit        | call solve_step_transient                    |
| IR + lumped thermal           | pdnkit        | superseded by mpkit per-cell thermal but     |
|                               |               | kept for the fast-iteration case             |
| Cavity Z(f)                   | pdnkit        | overlay on the same 3D scene as a slice plot |
| Full-wave RF (FDTD)           | sikit         | call sikit::fdtd, render |E|, |H|            |
| Channel synthesis             | sikit         | per-trace, surfaced as material loss inputs  |
| EMI board emissions           | emikit        | overlay current-density vectors              |
| 3D per-cell steady thermal    | mpkit (new)   | own solver -- pdnkit only has lumped         |
| 3D per-cell transient thermal | mpkit (new)   | implicit time stepping                       |
| Linear elasticity             | mpkit (new)   | static + thermal-expansion source            |
| Multiphysics couplings        | mpkit         | the orchestrator -- this is the value-add    |

## Voxel grid is the lingua franca

mpkit standardizes on a uniform Cartesian voxel grid. Every solver
reads from and writes to fields on the same grid; couplings between
physics are just field-to-field assignments at run time.

sikit's existing `Field3D` + `GridSpec` already match this shape.
Promote them to a new `circuitcore::field` library so mpkit and sikit
both use the same types -- one allocation, one indexing convention,
zero copy when handing a field from sikit to mpkit.

Tradeoffs:

- Voxels miss curved geometry (staircase error). Same trade FDTD makes.
  Acceptable for a v1; tet meshing is a 6-month rabbit hole.
- Resolution caps at memory. 1 mm cells for a 100x100 mm board is 1 M
  voxels per layer x N layers; a 256 MB working set keeps real boards
  in scope without paging.

## Couplings (multiphysics in mpkit's sense)

A Coupling is a small object that reads one field and writes another.
The orchestrator chains them in dependency order, runs the next solver,
repeats if needed for nonlinear convergence.

v1 ships:

1. **Joule heating**: pdnkit IR Solution -> per-voxel power density
   field -> thermal source term.
2. **Thermal-conductivity feedback**: temperature field -> copper rho
   update -> re-run IR. Equivalent to pdnkit's lumped thermal loop but
   per-cell.
3. **Thermal expansion**: temperature field -> isotropic strain source
   for the elasticity solver.

Each coupling is one .h/.cpp file; new physics adds new couplings.

## Studio Mp tab layout (COMSOL crib)

- Left dock: Study tree -- Geometry, Materials, Physics, Couplings,
  Solvers, Sweeps, Results. Right-click adds children.
- Center: 3D viewer. Camera with orbit (LMB), pan (MMB), dolly (wheel).
  Persp/ortho toggle. XYZ gizmo overlay. Field colormap with adjustable
  range, iso-surfaces, slice planes (axis + arbitrary), volume render,
  vector arrows, streamlines, clip planes, animation slider.
- Right dock: Settings for whatever node is selected in the tree.
- Bottom dock: Solver log + per-physics convergence plots.

The viewer is the single biggest chunk of new code. Built on the same
QOpenGLWidget + camera infrastructure sikit's 3D view already uses;
adds the COMSOL-style field-rendering primitives on top.

## Out of scope for v1

- Adaptive mesh refinement, unstructured tet meshes
- Nonlinear materials (plasticity, hyperelasticity)
- Turbulent CFD
- Phase change / contact
- Symbolic equation entry (COMSOL's "Coefficient Form PDE")
- Optimization / parameter estimation

These are all on the table for v2+; the voxel-grid + Coupling
foundation is chosen to support them without re-architecting.

## Dependency graph after mpkit lands

```
                       circuitcore::field          <- new
                            ^         ^
                            |         |
            sikit::fdtd ----+         +---- mpkit::core (Grid, Material, BC,
                                                          Voxelizer, Library)
                                                              ^
                                                              |
  pdnkit::pi --calls--> mpkit::couple --calls--> mpkit::thermal
                                                  mpkit::mech
                                                  ...
                                                              ^
                                                              |
                                              mpkit::widgets (3D viewer)
                                                              ^
                                                              |
                                                  circuitcore_studio MpTab
```

No back-edges; mpkit depends on the other kits, never the reverse.

## Acceptance for the first PR

This PR ships:

- `circuitcore::field` library (Field3D, GridSpec, cfl_dt promoted from
  sikit::fdtd; sikit retains a using-declaration so nothing else needs
  touching).
- `mpkit::core`: Grid, Material, MaterialLibrary (copper, FR-4, air,
  silver, aluminium, glass-epoxy), BoundaryCondition (Dirichlet,
  Neumann, Robin), Voxelizer (Board -> per-voxel material id).
- Tests: indexing round-trip, material library lookup, voxelizer
  produces sane material assignments for the tiny test board.
- No solver, no viewer, no studio tab yet -- those land in follow-up
  PRs.

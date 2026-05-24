# pdnkit architecture

Map of the pdnkit codebase.

## Layout

```
pdnkit/
├── pi/                math, no Qt dep, linked by tests
│   ├── IrMesher       zones/segments -> resistor network
│   ├── IrSolver       sparse Cholesky on G*v = i
│   ├── CavityModel    plane Z(f), mode sum
│   ├── DecapOptimizer greedy decap placement
│   ├── Transient      backward Euler
│   └── PowerDrc       IPC-2152 width check
├── render/            tessellation, camera, heat-map mesh
└── *.cpp, *.h         Qt GUI (MainWindow, PcbCanvas, panels)
```

`pdnkit_pi` (static lib) has no Qt dependency. Linked by both the GUI binary and the tests.

## Data flow

```
file.kicad_pcb
  -> circuitcore::formats::kicad::PcbParser::parse_file
        returns std::expected<Board, ParseError>

  Board
    -> render::build_all_meshes -> vector<LayerMesh>
                                    -> PcbCanvas board buffer
    -> pi::IrMesher::build(MeshConfig)
          -> pi::IrMesh
              -> pi::IrSolver::solve -> pi::Solution (V per node)
                  -> render::build_ir_result_mesh
                      -> IrResultMesh (quad per node, V normalized)
                          -> PcbCanvas heat buffer
```

## Conventions

Units SI throughout (m, rad, A, V). KiCad mm and degrees convert at parse time.

KiCad Y grows downward. `Camera2D::ortho_matrix` flips Y so OpenGL renders right-side-up.

Naming: `PascalCase` types, `snake_case` functions, trailing underscore on members, lowercase namespaces (`pdnkit::pi`, `pdnkit::render`).

Memory: raw pointers for non-owning Qt parent/child, `unique_ptr` for owned heap allocations, values otherwise.

Errors: solvers return `Solution { ok, error, ... }`. The kicad parser returns `std::expected<Board, ParseError>`. The GUI catches at the action boundary and shows a `QMessageBox`.

## Math

- Static IR drop: sparse Cholesky on G*v = i. Wadell, *Transmission Line Design Handbook*, sec 3.2.
- Plane Z(f): segmentation + cavity model. Okoshi 1972; Swaminathan, *Power Integrity Modeling and Design for Semiconductors and Systems*.
- IPC-2152 trace width: IPC-2221 closed form `I = k * dT^0.44 * A^0.725`, k=0.048 external / 0.024 internal.

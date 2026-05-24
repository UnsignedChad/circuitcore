# CircuitCore

Open-source toolkit for **PCB power-integrity** and **signal-integrity**
analysis. C++20 / Qt6 / Eigen. Reads native PCB files, runs physics
solvers, shows results.

## Tools

| Tool | What it does |
|---|---|
| [**pdnkit**](pdnkit/) | Power Integrity: static IR drop, plane Z(f), time-domain transient. Heat-map overlay, decap optimizer, hotspot finder. |
| [**sikit**](sikit/)   | Signal Integrity: trace impedance, S-parameters, eye diagrams, TDR/TDT. *(Migration in progress -- see [sikit/MIGRATION.md](sikit/MIGRATION.md).)* |

## Layout

```
circuitcore/
├── board/             canonical PCB model + hit-test     circuitcore::board::
├── sexpr/             generic S-expression reader        circuitcore::sexpr::
├── formats/           one subfolder per PCB format adapter
│   └── kicad/         .kicad_pcb -> board::Board         circuitcore::formats::kicad::
├── pdnkit/            Power Integrity tool               pdnkit::pi::
├── sikit/             Signal Integrity tool              sikit::si::
└── third_party/       vendored single-header deps
```

The layering is intentional: **every format adapter under `formats/`
produces a canonical `circuitcore::board::Board`**. The analysis tools
(`pdnkit/`, `sikit/`) only know about that canonical model -- they never
touch a file format. Adding Altium, Eagle, or IPC-2581 support means
adding one folder under `formats/`; nothing else changes.

## Build

```
sudo apt install -y qt6-base-dev qt6-base-dev-tools \
                    libqt6opengl6-dev libqt6openglwidgets6 \
                    libeigen3-dev libsuitesparse-dev libcgal-dev \
                    libspdlog-dev libcli11-dev libboost-dev \
                    ninja-build cmake clang catch2

CC=clang CXX=clang++ cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
```

## License

GPL-3.0.

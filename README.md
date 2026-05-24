# CircuitCore

PCB analysis toolkit. C++23, Qt6, Eigen.

- [pdnkit](pdnkit/): power integrity. IR drop, plane Z(f), transient, IPC-2152.
- [sikit](sikit/): signal integrity. Impedance, S-parameters, eye, TDR.

```
circuitcore/
├── board/             canonical PCB model
├── sexpr/             S-expression reader
├── formats/kicad/     .kicad_pcb -> board::Board
├── pdnkit/
├── sikit/
└── third_party/
```

New file formats slot in under `formats/`. Analysis tools never see a file format directly; they only know `circuitcore::board::Board`.

## Build (Ubuntu 24.04)

```
sudo apt install -y qt6-base-dev qt6-base-dev-tools \
    libqt6opengl6-dev libqt6openglwidgets6 \
    libeigen3-dev libsuitesparse-dev libcgal-dev \
    libspdlog-dev libcli11-dev libboost-dev \
    ninja-build cmake clang-18 catch2

CC=clang-18 CXX=clang++-18 cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
```

GPL-3.0.

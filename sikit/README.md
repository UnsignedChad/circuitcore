# sikit (in this monorepo)

The Signal Integrity tool. This directory is reserved for sikit's
source. **Migration is owned by the sikit agent** -- see
[MIGRATION.md](MIGRATION.md) for the step-by-step.

When migration is complete, this directory will look like pdnkit's:

```
sikit/
├── si/                   sikit::si namespace -- Wadell, MoM, etc.
├── render/               sikit::render -- 3D, S-param plots, eye
├── *.cpp/.h              UI: MainWindow, panels, canvas
├── main.cpp
├── tests/
└── CMakeLists.txt
```

Until then, `add_subdirectory(sikit)` in the top-level CMakeLists.txt
is commented out; `cmake --build build` only builds pdnkit + circuitcore.

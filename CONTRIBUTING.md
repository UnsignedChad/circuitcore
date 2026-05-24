# Contributing

| Change | Where it goes |
|---|---|
| Canonical PCB model, hit-test | `board/` |
| New file format | `formats/<vendor>/` |
| Generic utility used by multiple formats | top-level (e.g. `sexpr/`) |
| Power integrity | `pdnkit/` |
| Signal integrity | `sikit/` |
| Vendored single-header dep | `third_party/<org>/` |

The bar for `board/` and top-level utilities: every format adapter and every analysis tool needs it. If it is specific to one tool, it goes in that tool.

## Build

```
CC=clang-18 CXX=clang++-18 cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Tests live next to the code they cover (`sexpr/tests/`, `formats/kicad/tests/`, `pdnkit/tests/`, `sikit/tests/`). Filter with `ctest -R`.

## Format adapters

Each `formats/<vendor>/` exposes one function: take a file path, return `std::expected<circuitcore::board::Board, ParseError>`. The kicad adapter is the reference.

## Style

C++23. clang >= 17 or gcc >= 13. `-Wall -Wextra -Wpedantic`. `clang-format` is configured; run before pushing.

Types `PascalCase`, functions `snake_case`, members trailing underscore. Namespaces lowercase: `circuitcore::board`, `circuitcore::sexpr`, `circuitcore::formats::kicad`, `pdnkit::pi`, `sikit::si`.

## PRs

See [.github/POLICY.md](.github/POLICY.md). GPL-3.0.

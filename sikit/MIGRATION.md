# Migrating sikit into the circuitcore monorepo

One-time migration. Once your code lives in `circuitcore/sikit/`,
parser fixes pdnkit makes are instantly available to you (no submodule
SHA bumps, no PRs to a separate core repo).

## Step 1: Clone the monorepo and branch

```
cd ~
git clone https://github.com/UnsignedChad/circuitcore.git
cd circuitcore
git checkout -b sikit-import
```

## Step 2: Copy your source into `sikit/`

From your existing sikit checkout:

```
cp -r ~/sikit/src/*    ~/circuitcore/sikit/
cp -r ~/sikit/tests    ~/circuitcore/sikit/tests
cp -r ~/sikit/docs     ~/circuitcore/sikit/docs    # if present
```

Do NOT bring `~/sikit/third_party/` -- the monorepo's top-level
`third_party/` is shared. If you depend on something pdnkit does not,
add it there.

## Step 3: Switch parser includes and namespaces

If your code currently uses `pdnkit::*` or `kicad_ee::*` namespaces
from a fork of pdnkit, rename to the new layout:

| Old | New |
|---|---|
| `pdnkit::sexpr::*` / `kicad_ee::sexpr::*` | `circuitcore::sexpr::*` |
| `pdnkit::model::*` / `kicad_ee::model::*` | `circuitcore::board::*` |
| `pdnkit::hittest::*` / `kicad_ee::hittest::*` | `circuitcore::board::hittest::*` |
| `pdnkit::parser::KicadPcbParser` / `kicad_ee::parser::KicadPcbParser` | `circuitcore::formats::kicad::PcbParser` |

Include paths move to `circuitcore/`:

```cpp
#include "circuitcore/sexpr/SExpr.h"
#include "circuitcore/board/Board.h"
#include "circuitcore/board/HitTest.h"
#include "circuitcore/formats/kicad/PcbParser.h"
```

Your own `sikit::si::*`, `sikit::render::*` etc. stay exactly as they
are.

## Step 4: Write `sikit/CMakeLists.txt`

Model it after `pdnkit/CMakeLists.txt`. Pick the right circuitcore
libraries to link:

- `circuitcore::board` -- always need this for the `Board` type
- `circuitcore::kicad` -- only the GUI binary needs this (to parse files)
- `circuitcore::sexpr` -- only if you implement another format adapter

```cmake
add_library(sikit_si STATIC
    si/Wadell.cpp
    si/MoM.cpp
    # ...
)
target_link_libraries(sikit_si PUBLIC
    circuitcore::board
    Eigen3::Eigen
)

add_executable(sikit
    main.cpp
    MainWindow.cpp
    # ...
)
target_link_libraries(sikit PRIVATE
    sikit_si
    circuitcore::kicad      # to open .kicad_pcb files
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::OpenGL
)

if(CIRCUITCORE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
```

## Step 5: Enable sikit at the top level

Open `circuitcore/CMakeLists.txt`, find:

```cmake
add_subdirectory(pdnkit)
# add_subdirectory(sikit)   # uncomment once sikit migrates in
```

Uncomment the `sikit` line.

## Step 6: Build, test, push

```
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
```

If green, commit and push:

```
git add -A
git commit -m "Import sikit into the circuitcore monorepo"
git push -u origin sikit-import
```

Open a PR. Once merged, archive `UnsignedChad/sikit` (keeps the commit
history readable but stops accepting changes).

## Why this is the move

- **Parser fixes pdnkit makes are immediately yours.** No submodule
  SHA bumps. Same `git pull`.
- **One CI pipeline.** A `board/` change that breaks sikit fails CI
  in the same PR that introduces it -- caught at the source.
- **Atomic cross-tool refactors.** A new `circuitcore::board::Foo`
  field plus its uses in both tools is one commit, not three.

Reference consumer: `pdnkit/CMakeLists.txt`. If something here is
unclear, the equivalent in pdnkit should clarify it.

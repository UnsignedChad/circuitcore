# circuitcore::ui -- shared rendering modules for the Qt-based GUIs.
#
# Started as a consolidation: sikit/render/ and pdnkit/render/ each had
# their own copy of the same Camera2D / SegmentMesher / ViaMesher / ZoneMesher / CircleHelper /
# LayerColors files (modulo namespace). They drifted -- pdnkit's ViaMesher
# grew SMD-rect support that sikit's didn't have. This library is the
# single canonical home so the next drift can't happen.
#
# What's NOT here yet:
#   - PcbCanvas itself. sikit and pdnkit each subclass-flavour theirs
#     with very different overlays (impedance vs IR-drop, decap
#     markers, cavity highlight, etc.). The base+subclass refactor is
#     the next PR; this one is just the renderer-module move so the
#     diff is reviewable.
#   - IrResultMesh (pdnkit-only, has IrMesher deps). Stays in pdnkit/.

add_library(circuitcore_ui STATIC
    src/Camera2D.cpp
    src/SegmentMesher.cpp
    src/ViaMesher.cpp
    src/ZoneMesher.cpp
    src/PcbCanvas.cpp
    include/circuitcore/ui/PcbCanvas.h
)
add_library(circuitcore::ui ALIAS circuitcore_ui)

set_target_properties(circuitcore_ui PROPERTIES AUTOMOC ON)

target_include_directories(circuitcore_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
# earcut.hpp lives under third_party/ at the repo root (header-only).
target_include_directories(circuitcore_ui PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(circuitcore_ui
    PUBLIC
        circuitcore::board
        Qt6::Gui          # QMatrix4x4 / QVector* in Camera headers
        Qt6::OpenGLWidgets   # PcbCanvas extends QOpenGLWidget
)

target_compile_options(circuitcore_ui PRIVATE -Wall -Wextra -Wpedantic)

# circuitcore::kicad -- adapter from .kicad_pcb to circuitcore::board::Board.
# Returns a board model, so PUBLIC-links board (the type leaks through the API).
# Uses sexpr internally; PRIVATE link is enough.
add_library(circuitcore_kicad STATIC
    src/PcbParser.cpp
)
add_library(circuitcore::kicad ALIAS circuitcore_kicad)

target_include_directories(circuitcore_kicad PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(circuitcore_kicad
    PUBLIC  circuitcore::board
    PRIVATE circuitcore::sexpr
)
target_compile_options(circuitcore_kicad PRIVATE -Wall -Wextra -Wpedantic)

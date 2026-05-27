# circuitcore::board -- canonical PCB model + hit-test.
# Format-independent. Any format adapter produces a circuitcore::board::Board.
add_library(circuitcore_board STATIC
    src/Bounds.cpp
    src/HitTest.cpp
    src/PackageDefaults.cpp
)
add_library(circuitcore::board ALIAS circuitcore_board)

target_include_directories(circuitcore_board PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
circuitcore_warnings(circuitcore_board)

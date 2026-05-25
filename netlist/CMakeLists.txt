# circuitcore::netlist -- schematic-derived netlist data model.
# Format-independent. Any format adapter (NetlistParser in formats/kicad,
# future Altium / OrcadPCB2 readers) produces this shape.
add_library(circuitcore_netlist STATIC
    src/Netlist.cpp
)
add_library(circuitcore::netlist ALIAS circuitcore_netlist)

target_include_directories(circuitcore_netlist PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
circuitcore_warnings(circuitcore_netlist)

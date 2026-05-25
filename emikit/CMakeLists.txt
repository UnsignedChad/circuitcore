# emikit -- radiated emissions pre-compliance tool.
# Math library + a CLI binary. No Qt in v1.

add_library(emikit_emi STATIC
    emi/Spectrum.cpp
    emi/LoopEmissions.cpp
    emi/CableCommonMode.cpp
    emi/Masks.cpp
    emi/BoardAnalysis.cpp
)
target_include_directories(emikit_emi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(emikit_emi PUBLIC circuitcore::board)
circuitcore_warnings(emikit_emi)

add_executable(emikit
    main.cpp
)
target_link_libraries(emikit PRIVATE
    emikit_emi
    circuitcore::kicad
    CLI11::CLI11
)
circuitcore_warnings(emikit)

add_executable(emikit_validate_ti
    tools/validate_ti.cpp
)
target_link_libraries(emikit_validate_ti PRIVATE emikit_emi)
circuitcore_warnings(emikit_validate_ti)

if(CIRCUITCORE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

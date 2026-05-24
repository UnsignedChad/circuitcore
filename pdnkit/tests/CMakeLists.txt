add_executable(pdnkit_tests
    smoke_test.cpp
    model_test.cpp
    camera_test.cpp
    zone_mesher_test.cpp
    segment_mesher_test.cpp
    via_mesher_test.cpp
    hit_test.cpp
    ir_mesher_test.cpp
    ir_solver_test.cpp
    ir_result_mesh_test.cpp
    cavity_model_test.cpp
    decap_optimizer_test.cpp
    transient_test.cpp
    e2e_test.cpp
    ohms_law_test.cpp
    power_drc_test.cpp
    spice_export_test.cpp
    target_z_test.cpp
    via_inductance_test.cpp
)
target_compile_definitions(pdnkit_tests PRIVATE
    PDNKIT_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(pdnkit_tests PRIVATE
    pdnkit_pi
    circuitcore::kicad
    Catch2::Catch2WithMain
)
target_compile_options(pdnkit_tests PRIVATE
    -Wall -Wextra -Wpedantic
)
catch_discover_tests(pdnkit_tests)

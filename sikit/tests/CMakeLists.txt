# sikit unit tests. AMI dlopen tests link a small C stub from stub_ami.c.

# Standalone AMI stub library used by the AMI loader test. Built as a
# stand-alone .so so dlopen() can pick it up at runtime. -- needs to opt out
# of AUTOMOC since there's no Qt content (CMake otherwise tries to MOC the
# .c file and fails).
set_source_files_properties(stub_ami.c PROPERTIES LANGUAGE CXX)
add_library(stub_ami_sikit SHARED stub_ami.c)
set_target_properties(stub_ami_sikit PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    OUTPUT_NAME stub_ami)
target_compile_options(stub_ami_sikit PRIVATE -fPIC)

add_executable(sikit_tests
    smoke_test.cpp
    camera_test.cpp
    camera3d_test.cpp
    segment_mesher_test.cpp
    via_mesher_test.cpp
    zone_mesher_test.cpp
    mesher3d_test.cpp
    em2d_test.cpp
    rlgc_test.cpp
    fdm_refined_test.cpp
    impedance_test.cpp
    trace_impedance_test.cpp
    diff_pair_test.cpp
    diff_synth_test.cpp
    crosstalk_test.cpp
    vector_fit_test.cpp
    spice_export_test.cpp
    compliance_test.cpp
    topology_test.cpp
    headless_test.cpp
    via_model_test.cpp
    roughness_test.cpp
    channel_test.cpp
    channel_synthesis_test.cpp
    channel_dispersion_test.cpp
    djordjevic_sarkar_test.cpp
    sparam_test.cpp
    fft_test.cpp
    eye_test.cpp
    eye_ramp_test.cpp
    eye_metrics_test.cpp
    eye_mask_test.cpp
    bathtub_test.cpp
    stat_eye_test.cpp
    touchstone_test.cpp
    touchstone_v2_test.cpp
    touchstone_writer_test.cpp
    touchstone_csv_test.cpp
    ibis_test.cpp
    ami_test.cpp
    project_test.cpp
    reference_data_test.cpp
)
target_link_libraries(sikit_tests PRIVATE
    sikit_si
    circuitcore::kicad
    Catch2::Catch2WithMain
    ${CMAKE_DL_LIBS}
)
target_compile_definitions(sikit_tests PRIVATE
    SIKIT_TEST_STUB_AMI_PATH="$<TARGET_FILE:stub_ami_sikit>"
)
add_dependencies(sikit_tests stub_ami_sikit)
target_compile_options(sikit_tests PRIVATE -Wall -Wextra -Wpedantic)
catch_discover_tests(sikit_tests)

# sikit -- Signal Integrity tool. Builds sikit_si static lib + sikit
# GUI executable + tests.
#
# Mirrors pdnkit's structure: a Qt-free math/render lib (sikit_si)
# linked into the GUI executable (sikit). circuitcore::board is PUBLIC-
# linked because Board types leak through sikit_si's public API; the
# .kicad_pcb parser is PRIVATE on sikit_si because only the GUI binary
# actually opens files.

add_library(sikit_si STATIC
    # SI math
    si/Ami.cpp
    si/Bathtub.cpp
    si/ChannelResponse.cpp
    si/ChannelSynthesis.cpp
    si/ReturnPath.cpp
    si/BusGroup.cpp
    si/Overlay.cpp
    si/Crosstalk.cpp
    si/VectorFit.cpp
    si/SpiceExport.cpp
    si/CrossSection.cpp
    si/DiffPair.cpp
    si/DiffSynth.cpp
    si/DjordjevicSarkar.cpp
    si/Eye.cpp
    si/EyeMask.cpp
    si/Compliance.cpp
    si/Topology.cpp
    si/HeadlessOps.cpp
    si/EyeMetrics.cpp
    si/FdmSolver.cpp
    si/Fft.cpp
    si/Ibis.cpp
    si/Impedance.cpp
    si/Project.cpp
    si/Rlgc.cpp
    si/SParam.cpp
    si/SiStackup.cpp
    si/Skew.cpp
    si/StatEye.cpp
    si/SurfaceRoughness.cpp
    si/Touchstone.cpp
    si/TouchstoneCsv.cpp
    si/TouchstoneWriter.cpp
    si/TraceImpedance.cpp
    si/ViaModel.cpp
    # Rendering
    render/Camera2D.cpp
    render/Camera3D.cpp
    render/Mesher3D.cpp
    render/SegmentMesher.cpp
    render/ViaMesher.cpp
    render/ZoneMesher.cpp
)
target_include_directories(sikit_si PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(sikit_si
    PUBLIC
        circuitcore::board
        circuitcore::sexpr   # SiStackup parses (setup (stackup ...)) directly
        Eigen3::Eigen
        spdlog::spdlog
        ${CMAKE_DL_LIBS}     # for AMI dlopen
)
target_compile_options(sikit_si PRIVATE -Wall -Wextra -Wpedantic)

# GUI binary. Needs the KiCad adapter to open files, plus Qt.
add_executable(sikit
    main.cpp
    MainWindow.cpp
    PcbCanvas.cpp
    LayerPanel.cpp
    EyeWindow.cpp
    SParamPlotWindow.cpp
)
set_target_properties(sikit PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)
target_link_libraries(sikit PRIVATE
    sikit_si
    circuitcore::kicad
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::OpenGL
)
target_compile_options(sikit PRIVATE -Wall -Wextra -Wpedantic)

if(CIRCUITCORE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

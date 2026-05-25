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
    si/Connector.cpp
    si/Report.cpp
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
    si/Fdtd3d.cpp
    si/FdtdRasterize.cpp
    si/FdtdPort.cpp
    si/SchematicTopology.cpp
    si/SiStackup.cpp
    si/Skew.cpp
    si/StatEye.cpp
    si/SurfaceRoughness.cpp
    si/Touchstone.cpp
    si/TouchstoneCsv.cpp
    si/TouchstoneWriter.cpp
    si/TraceImpedance.cpp
    si/ViaModel.cpp
    # 3D renderer stays sikit-side -- depends on SiStackup
    render/Camera3D.cpp
    render/Mesher3D.cpp
)
target_include_directories(sikit_si PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(sikit_si
    PUBLIC
        circuitcore::board
        circuitcore::sexpr   # SiStackup parses (setup (stackup ...)) directly
        circuitcore::netlist # SchematicTopology consumes parsed netlists
        circuitcore::ui      # shared Qt renderer (Camera, meshers, 3D)
        Eigen3::Eigen
        spdlog::spdlog
        ${CMAKE_DL_LIBS}     # for AMI dlopen
)
target_compile_options(sikit_si PRIVATE -Wall -Wextra -Wpedantic)

# Embeddable Qt widgets. Separated from the GUI executable so that
# downstream apps (the studio shell, future tools) can link them without
# pulling in sikit/main.cpp + MainWindow.cpp.
add_library(sikit_widgets STATIC
    EyeWindow.cpp
    EyeWindow.h
    SParamPlotWindow.cpp
    SParamPlotWindow.h
    PcbCanvas.cpp
    PcbCanvas.h
    LayerPanel.cpp
    LayerPanel.h
)
add_library(sikit::widgets ALIAS sikit_widgets)
set_target_properties(sikit_widgets PROPERTIES AUTOMOC ON)
target_include_directories(sikit_widgets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}    # so studio can use "sikit/EyeWindow.h" etc.
)
target_link_libraries(sikit_widgets PUBLIC
    sikit_si
    circuitcore::ui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::OpenGL
)
target_compile_options(sikit_widgets PRIVATE -Wall -Wextra -Wpedantic)

# GUI binary. Needs the KiCad adapter to open files, plus Qt.
add_executable(sikit
    main.cpp
    MainWindow.cpp
)
set_target_properties(sikit PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)
target_link_libraries(sikit PRIVATE
    sikit_si
    sikit::widgets
    circuitcore::ui      # PcbCanvas now subclasses circuitcore::ui::PcbCanvas
    circuitcore::kicad
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::OpenGL
)
target_compile_options(sikit PRIVATE -Wall -Wextra -Wpedantic)

if(CIRCUITCORE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

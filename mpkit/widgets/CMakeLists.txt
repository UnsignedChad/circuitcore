# mpkit::widgets -- COMSOL-style 3D viewer built on QVTKOpenGLNativeWidget.
#
# VTK is the right tool for scientific volume rendering: native support
# for vtkImageData, GPU volume mappers, iso-surfaces via marching cubes,
# arbitrary slice planes, vector arrows, streamlines, threshold filters,
# colour-by-scalar lookup tables. Wrapping it in Qt via
# QVTKOpenGLNativeWidget gives us first-class integration with the rest
# of the circuitcore_studio shell.
#
# Qt5 / Qt6 trap: Debian/Ubuntu apt ships libvtk9-qt-dev linked against
# Qt5, which silently miscompiles when mixed into a Qt6 project. We
# refuse to use a system VTK whose GUISupportQt was built against Qt5
# and fall back to building VTK from source against Qt6 via
# FetchContent. First configure on a fresh machine takes ~15-30 min
# (VTK is big); subsequent builds reuse the cached build tree, and CI
# caches the FetchContent download + build artefacts.

set(MPKIT_VTK_REQUIRED_COMPONENTS
    CommonCore
    CommonColor
    CommonDataModel
    CommonExecutionModel
    FiltersCore
    FiltersGeometry
    FiltersModeling
    FiltersSources
    GUISupportQt
    InteractionStyle
    InteractionWidgets
    RenderingAnnotation
    RenderingCore
    RenderingOpenGL2
    RenderingVolume
    RenderingVolumeOpenGL2
)

# Always FetchContent. System VTK detection re-enters via the
# MPKIT_USE_SYSTEM_VTK option once vendors start shipping a Qt6 build;
# until then it bleeds Qt5 targets into the source build and breaks it.
option(MPKIT_USE_SYSTEM_VTK
       "Use a system-installed Qt6-built VTK instead of FetchContent."
       OFF)

if(MPKIT_USE_SYSTEM_VTK)
    find_package(VTK 9 REQUIRED COMPONENTS ${MPKIT_VTK_REQUIRED_COMPONENTS})
    message(STATUS "Using system VTK ${VTK_VERSION} (Qt6 build assumed)")
else()
    include(FetchContent)
    # MPI is pulled in transitively by some VTK modules; enable C here
    # so the MPI find module can probe for mpicc.
    enable_language(C)
    find_package(MPI COMPONENTS C)

    # Slim VTK config for the modules we actually need.
    set(BUILD_SHARED_LIBS         OFF CACHE BOOL "" FORCE)
    set(VTK_BUILD_TESTING         OFF CACHE BOOL "" FORCE)
    set(VTK_BUILD_EXAMPLES        OFF CACHE BOOL "" FORCE)
    set(VTK_BUILD_DOCUMENTATION   OFF CACHE BOOL "" FORCE)
    set(VTK_LEGACY_REMOVE         ON  CACHE BOOL "" FORCE)
    set(VTK_GROUP_ENABLE_StandAlone DONT_WANT CACHE STRING "" FORCE)
    set(VTK_GROUP_ENABLE_Imaging    DONT_WANT CACHE STRING "" FORCE)
    set(VTK_GROUP_ENABLE_MPI        DONT_WANT CACHE STRING "" FORCE)
    set(VTK_GROUP_ENABLE_Rendering  WANT      CACHE STRING "" FORCE)
    set(VTK_GROUP_ENABLE_Qt         WANT      CACHE STRING "" FORCE)
    set(VTK_QT_VERSION             "6"        CACHE STRING "" FORCE)

    # Disable the Qt submodules we do NOT use -- otherwise VTK looks
    # for Qt6::Quick / Qt6::Sql which most desktops do not install.
    set(VTK_MODULE_ENABLE_VTK_GUISupportQt      YES CACHE STRING "" FORCE)
    set(VTK_MODULE_ENABLE_VTK_GUISupportQtQuick NO  CACHE STRING "" FORCE)
    set(VTK_MODULE_ENABLE_VTK_GUISupportQtSQL   NO  CACHE STRING "" FORCE)
    set(VTK_WRAP_PYTHON           OFF CACHE BOOL "" FORCE)
    set(VTK_WRAP_JAVA             OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(VTK
        GIT_REPOSITORY https://gitlab.kitware.com/vtk/vtk.git
        GIT_TAG        v9.3.1
        GIT_SHALLOW    TRUE
    )
    message(STATUS "Fetching VTK v9.3.1 source -- first build takes a while")
    FetchContent_MakeAvailable(VTK)
endif()  # MPKIT_USE_SYSTEM_VTK

add_library(mpkit_widgets STATIC
    src/FieldViewer.cpp
    include/mpkit/widgets/FieldViewer.h
)
add_library(mpkit::widgets ALIAS mpkit_widgets)

target_include_directories(mpkit_widgets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(mpkit_widgets PROPERTIES
    AUTOMOC ON
    QT_MAJOR_VERSION 6
)

target_link_libraries(mpkit_widgets PUBLIC
    mpkit::mp
    circuitcore::field
    VTK::CommonCore
    VTK::CommonColor
    VTK::CommonDataModel
    VTK::CommonExecutionModel
    VTK::FiltersCore
    VTK::FiltersGeometry
    VTK::FiltersModeling
    VTK::FiltersSources
    VTK::GUISupportQt
    VTK::InteractionStyle
    VTK::InteractionWidgets
    VTK::RenderingAnnotation
    VTK::RenderingCore
    VTK::RenderingOpenGL2
    VTK::RenderingVolume
    VTK::RenderingVolumeOpenGL2
    Qt6::Widgets
    Qt6::OpenGLWidgets
)

# VTK 9.1+ wants modules autoinit'd at link time.
vtk_module_autoinit(
    TARGETS mpkit_widgets
    MODULES
        VTK::CommonCore
        VTK::FiltersCore
        VTK::GUISupportQt
        VTK::InteractionStyle
        VTK::InteractionWidgets
        VTK::RenderingAnnotation
        VTK::RenderingCore
        VTK::RenderingOpenGL2
        VTK::RenderingVolume
        VTK::RenderingVolumeOpenGL2
)

circuitcore_warnings(mpkit_widgets)

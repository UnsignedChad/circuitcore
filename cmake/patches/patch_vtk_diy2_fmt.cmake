# Neutralize the _SECURE_SCL branch in VTK 9.3.1's bundled diy2 fmt
# header.
#
# Why: VS 17.10+ and VS 18 Insiders removed stdext::checked_array_iterator
# from <iterator>. The bundled fmt (vendored inside VTK ThirdParty/diy2)
# still references it under "#ifdef _SECURE_SCL", which MSVC defines in
# Debug builds. The opt-in flag _HAS_DEPRECATED_STDEXT_ARR_ITERS=1 no
# longer brings the type back -- it is gone, period.
#
# Patch: rewrite "#ifdef _SECURE_SCL" to "#if 0" so the broken branch is
# never taken. The matching "#else" branch uses plain pointers, which is
# what every non-MSVC build of fmt does anyway. No behaviour change for
# release builds (where _SECURE_SCL was already 0) or for non-MSVC
# toolchains.
#
# Idempotent: a second run is a no-op since the marker has already been
# replaced.

set(_fmt_h
    "${VTK_SOURCE_DIR}/ThirdParty/diy2/vtkdiy2/include/vtkdiy2/fmt/format.h")

if(NOT EXISTS "${_fmt_h}")
    message(STATUS
        "vtk-diy2-fmt patch: ${_fmt_h} not present, skipping")
    return()
endif()

file(READ "${_fmt_h}" _fmt_content)
string(REPLACE
    "#ifdef _SECURE_SCL"
    "#if 0  // _SECURE_SCL path disabled -- stdext::checked_array_iterator removed in new MSVC"
    _fmt_content_patched
    "${_fmt_content}")

if("${_fmt_content_patched}" STREQUAL "${_fmt_content}")
    message(STATUS "vtk-diy2-fmt patch: already applied (no #ifdef _SECURE_SCL)")
else()
    file(WRITE "${_fmt_h}" "${_fmt_content_patched}")
    message(STATUS "vtk-diy2-fmt patch: neutralized _SECURE_SCL branch")
endif()

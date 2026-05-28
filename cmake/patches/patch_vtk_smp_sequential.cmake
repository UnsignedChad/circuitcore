# Make VTK 9.3.1's SMP Sequential backend linkable on strict linkers.
#
# Why: vtkSMPToolsAPI.cxx references
# vtkSMPToolsImpl<BackendType::Sequential>::IsParallelScope() to
# dispatch at runtime. VTK 9.3.1 left the symbol unspecified for the
# Sequential backend -- the header defines the method inline as
#
#     bool IsParallelScope() { return this->IsParallel; }
#
# which compiles fine on gcc / clang (the inline gets emitted as a
# weak symbol and the linker is happy) but MSVC silently refuses to
# emit it for the Sequential template instantiation, leaving an
# unresolved external. The straightforward fix of writing an explicit
# .cxx specialization for Sequential does NOT work on MSVC -- the
# specialization is silently ignored when the base template already
# has an inline definition. (Earlier attempt: see commit history of
# this file.)
#
# Working fix: rewrite the inline header definition to return a
# constant. Sequential is by definition never inside a parallel
# scope, so `false` is correct. STDThread keeps its own explicit
# specialization in SMP/STDThread/vtkSMPToolsImpl.cxx which overrides
# the new inline default for that backend. TBB / OpenMP backends
# (not built in our config) would need similar overrides if anyone
# ever enables them.
#
# This script runs at every CMake configure. Idempotent -- the
# marker comment is checked before patching so a re-run is a no-op.

set(_hdr
    "${VTK_SOURCE_DIR}/Common/Core/SMP/Common/vtkSMPToolsImpl.h")

if(NOT EXISTS "${_hdr}")
    message(STATUS "vtk-smp-sequential patch: ${_hdr} not present, skipping")
    return()
endif()

file(READ "${_hdr}" _hdr_content)
if("${_hdr_content}" MATCHES "patched: Sequential default IsParallelScope")
    message(STATUS
        "vtk-smp-sequential patch: header already patched")
    return()
endif()

string(REPLACE
    "bool IsParallelScope() { return this->IsParallel; }"
    "// patched: Sequential default IsParallelScope -- avoids MSVC LNK2019.\n  // STDThread overrides via SMP/STDThread/vtkSMPToolsImpl.cxx.\n  bool IsParallelScope() { return false; }"
    _hdr_patched
    "${_hdr_content}")

if("${_hdr_patched}" STREQUAL "${_hdr_content}")
    message(STATUS
        "vtk-smp-sequential patch: header pattern not found "
        "(VTK source may have changed; review patch)")
    return()
endif()

file(WRITE "${_hdr}" "${_hdr_patched}")
message(STATUS
    "vtk-smp-sequential patch: rewrote IsParallelScope inline default in "
    "SMP/Common/vtkSMPToolsImpl.h to return false")

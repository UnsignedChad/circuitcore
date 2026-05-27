# Add missing IsParallelScope to VTK 9.3.1's Sequential SMP backend.
#
# Why: vtkSMPToolsAPI.cxx unconditionally references
# vtkSMPToolsImpl<BackendType::Sequential>::IsParallelScope() to
# dispatch at runtime, but the Sequential implementation file
# (SMP/Sequential/vtkSMPToolsImpl.cxx) ships only Initialize /
# GetEstimatedNumberOfThreads / GetSingleThread -- the IsParallelScope
# specialization was never written. The STDThread backend has it, so
# linking any binary whose dep graph reaches the SMP API path (eg
# vtkProbeOpenGLVersion) fails with LNK2019 on strict linkers (MSVC).
#
# Fix: append the missing template specialization to the Sequential
# .cxx so the symbol exists. Sequential by definition never runs in
# parallel scope, so the body just returns false.
#
# Idempotent: subsequent runs detect the marker and skip.

set(_seq_cxx
    "${VTK_SOURCE_DIR}/Common/Core/SMP/Sequential/vtkSMPToolsImpl.cxx")

if(NOT EXISTS "${_seq_cxx}")
    message(STATUS
        "vtk-smp-sequential patch: ${_seq_cxx} not present, skipping")
    return()
endif()

file(READ "${_seq_cxx}" _seq_content)
if("${_seq_content}" MATCHES "IsParallelScope")
    message(STATUS
        "vtk-smp-sequential patch: already applied (IsParallelScope found)")
    return()
endif()

# Inject the missing specialization right before VTK_ABI_NAMESPACE_END.
# A single-threaded backend is never in a parallel scope, so the body
# just returns false. Matches the signature in vtkSMPToolsImpl.h.
set(_inject
"\n//------------------------------------------------------------------------------\n// Patched in -- VTK 9.3.1 ships SMP/Sequential without an IsParallelScope\n// specialization; the strict linker on MSVC then cannot resolve the call\n// made by vtkSMPToolsAPI. Sequential is by definition not a parallel\n// scope, so this returns false.\ntemplate <>\nbool vtkSMPToolsImpl<BackendType::Sequential>::IsParallelScope()\n{\n  return false;\n}\n\n")

string(REPLACE
    "VTK_ABI_NAMESPACE_END"
    "${_inject}VTK_ABI_NAMESPACE_END"
    _seq_patched
    "${_seq_content}")

file(WRITE "${_seq_cxx}" "${_seq_patched}")
message(STATUS "vtk-smp-sequential patch: added IsParallelScope to Sequential backend")

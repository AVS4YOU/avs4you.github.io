cmake_minimum_required(VERSION 3.20)
project(StableAudio3Plugin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# The static runtime (/MT), the repository's default: the package carries no
# VC++ or UCRT DLLs (AVS installs the VC++ 2015-2022 Redistributable anyway,
# and the UCRT is part of Windows 10, the oldest the installers offer plugins
# on), and each backend is a single sa3.dll.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(STABLEAUDIO3_BACKEND "CPU" CACHE STRING "sa3 backend: CPU, VULKAN or NONE")
set_property(CACHE STABLEAUDIO3_BACKEND PROPERTY STRINGS CPU VULKAN NONE)

set(AVS_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk" CACHE PATH "AVS4YOU plugin SDK")
if(NOT EXISTS "${AVS_SDK_ROOT}/include/CContentPluginIntf.h")
  message(FATAL_ERROR "AVS SDK was not found at ${AVS_SDK_ROOT}")
endif()

add_library(StableAudio3 SHARED
  src/dllmain.cpp
  src/export_utils.cpp
  src/exports.cpp
  src/plugin.cpp
  src/sa3_backend.cpp
  src/system_info.cpp
  src/ui.cpp
  "${AVS_SDK_ROOT}/common/utils.cpp"
  "${AVS_SDK_ROOT}/translate/translate.cpp"
  "${AVS_SDK_ROOT}/ui/winapi/ui.cpp"
  StableAudio3.rc
  module.def)
target_include_directories(StableAudio3 PRIVATE "${AVS_SDK_ROOT}/include" "${CMAKE_CURRENT_SOURCE_DIR}/sa3-cpp/src")
target_compile_definitions(StableAudio3 PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set_property(SOURCE StableAudio3.rc APPEND PROPERTY COMPILE_DEFINITIONS
    "$<$<CONFIG:Release>:RC_X64>"
  )
endif()
target_link_libraries(StableAudio3 PRIVATE comctl32 dxgi winhttp shell32)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(STABLEAUDIO3_ARCH_DIR "x86")
else()
  set(STABLEAUDIO3_ARCH_DIR "x64")
endif()
set(STABLEAUDIO3_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/${STABLEAUDIO3_ARCH_DIR}")
set_target_properties(StableAudio3 PROPERTIES
  OUTPUT_NAME StableAudio3
  RUNTIME_OUTPUT_DIRECTORY "${STABLEAUDIO3_PACKAGE_DIR}"
  PDB_OUTPUT_DIRECTORY "${STABLEAUDIO3_PACKAGE_DIR}")
foreach(_configuration DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(StableAudio3 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_${_configuration} "${STABLEAUDIO3_PACKAGE_DIR}"
    PDB_OUTPUT_DIRECTORY_${_configuration} "${STABLEAUDIO3_PACKAGE_DIR}")
endforeach()

if(NOT STABLEAUDIO3_BACKEND STREQUAL "NONE")
  set(SA3_CUDA OFF CACHE BOOL "" FORCE)
  set(GGML_CUDA OFF CACHE BOOL "" FORCE)
  if(STABLEAUDIO3_BACKEND STREQUAL "VULKAN")
    # Vulkan reaches the GPU through the display driver of any vendor: no
    # CUDA runtime, and the shaders take a few MB where CUDA kernels took 200
    set(SA3_VULKAN ON CACHE BOOL "" FORCE)
    set(_sa3_destination "sa3-cpp-gpu")
  else()
    set(SA3_VULKAN OFF CACHE BOOL "" FORCE)
    set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
    set(_sa3_destination "sa3-cpp-cpu")
  endif()
  # ggml as static libraries inside sa3.dll: a backend is one file, and as
  # each folder's sa3.dll is a module of its own, the host can switch between
  # CPU and GPU without a restart (separate ggml.dll files share one name, so
  # Windows kept the first one loaded). ggml's own thread pool instead of
  # OpenMP, whose vcomp140.dll has no static form.
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
  # a CPU backend built for another instruction set lands in a folder of its
  # own: sa3-cpp-cpu-compat, which the plugin picks on CPUs without AVX2
  set(STABLEAUDIO3_BACKEND_FOLDER "" CACHE STRING
    "folder next to StableAudio3.dll the backend lands in (default sa3-cpp-cpu / sa3-cpp-gpu)")
  if(STABLEAUDIO3_BACKEND_FOLDER)
    set(_sa3_destination "${STABLEAUDIO3_BACKEND_FOLDER}")
  endif()
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  # ggml defaults to GGML_NATIVE, i.e. the build machine's CPU: built on an
  # AVX-512 machine, ggml-cpu.dll dies with an illegal instruction elsewhere.
  # Off, ggml targets AVX2/FMA/F16C - unless a preset turns those off too, as
  # the *-compat ones do.
  set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    # 32-bit MSVC has no _pdep_u64, so the BMI2 path does not link
    set(GGML_BMI2 OFF CACHE BOOL "" FORCE)
  endif()
  add_subdirectory(sa3-cpp EXCLUDE_FROM_ALL)
  add_custom_target(sa3_backend ALL DEPENDS sa3_shared)
  add_custom_command(TARGET sa3_backend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:StableAudio3>/${_sa3_destination}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:sa3_shared>" "$<TARGET_FILE_DIR:StableAudio3>/${_sa3_destination}/"
    VERBATIM)
endif()
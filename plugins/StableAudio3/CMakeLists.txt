cmake_minimum_required(VERSION 3.20)
project(StableAudio3Plugin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(MSVC)
  set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
  set(CMAKE_INSTALL_OPENMP_LIBRARIES TRUE)
  include(InstallRequiredSystemLibraries)
endif()

set(STABLEAUDIO3_BACKEND "CPU" CACHE STRING "sa3 backend: CPU, CUDA or NONE")
set_property(CACHE STABLEAUDIO3_BACKEND PROPERTY STRINGS CPU CUDA NONE)

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
  if(STABLEAUDIO3_BACKEND STREQUAL "CUDA")
    set(SA3_CUDA ON CACHE BOOL "" FORCE)
    set(_sa3_destination "sa3-cpp-gpu")
    set(_sa3_extra_backend ggml-cuda)
  else()
    set(SA3_CUDA OFF CACHE BOOL "" FORCE)
    set(GGML_CUDA OFF CACHE BOOL "" FORCE)
    set(_sa3_destination "sa3-cpp-cpu")
    set(_sa3_extra_backend "")
  endif()
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  add_subdirectory(sa3-cpp EXCLUDE_FROM_ALL)
  add_custom_target(sa3_backend ALL DEPENDS sa3_shared)
  add_custom_command(TARGET sa3_backend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:StableAudio3>/${_sa3_destination}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:sa3_shared>" "$<TARGET_FILE_DIR:StableAudio3>/${_sa3_destination}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ggml>" "$<TARGET_FILE_DIR:StableAudio3>/${_sa3_destination}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ggml-base>" "$<TARGET_FILE_DIR:StableAudio3>/${_sa3_destination}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ggml-cpu>" "$<TARGET_FILE_DIR:StableAudio3>/${_sa3_destination}/"
    VERBATIM)
  foreach(_runtime_library IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    add_custom_command(TARGET sa3_backend POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_runtime_library}"
              "$<TARGET_FILE_DIR:StableAudio3>/${_sa3_destination}/"
      VERBATIM)
  endforeach()
  if(STABLEAUDIO3_BACKEND STREQUAL "CUDA")
    add_custom_command(TARGET sa3_backend POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ggml-cuda>" "$<TARGET_FILE_DIR:StableAudio3>/${_sa3_destination}/")
  endif()
endif()
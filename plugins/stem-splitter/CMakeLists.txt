cmake_minimum_required(VERSION 3.20)
project(StemSplitterPlugin LANGUAGES C CXX)

# The plugin DLL only. stems.exe and onnxruntime.dll are third-party: fetch.py
# builds them into build/<arch>/, and build.py packs them with ffmpeg.exe.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# the static CRT, like every plugin here: no VC++ redistributable needed
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(AVS_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk" CACHE PATH "AVS4YOU plugin SDK")

add_library(StemSplitter SHARED
  src/dllmain.cpp
  src/export_utils.cpp
  src/exports.cpp
  src/plugin.cpp
  src/stems.cpp
  src/ui.cpp
  "${AVS_SDK_ROOT}/common/utils.cpp"
  "${AVS_SDK_ROOT}/common/ChildProcess.cpp"
  "${AVS_SDK_ROOT}/common/HttpDownload.cpp"
  "${AVS_SDK_ROOT}/translate/translate.cpp"
  "${AVS_SDK_ROOT}/ui/winapi/ui.cpp"
  StemSplitter.rc
  module.def)
target_include_directories(StemSplitter PRIVATE "${AVS_SDK_ROOT}/include")
target_compile_definitions(StemSplitter PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_compile_options(StemSplitter PRIVATE /utf-8)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set_property(SOURCE StemSplitter.rc APPEND PROPERTY COMPILE_DEFINITIONS "RC_X64")
  set(STEM_SPLITTER_ARCH x64)
else()
  set(STEM_SPLITTER_ARCH x86)
endif()
target_link_libraries(StemSplitter PRIVATE comctl32 comdlg32 shell32 winhttp)

# next to the tools fetch.py built, which is what the package is made of
set_target_properties(StemSplitter PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/build/${STEM_SPLITTER_ARCH}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/build/${STEM_SPLITTER_ARCH}/debug")

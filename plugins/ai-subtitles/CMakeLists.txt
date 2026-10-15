cmake_minimum_required(VERSION 3.20)
project(AiSubtitlesPlugin LANGUAGES C CXX)

# The plugin DLL only. whisper-cli is third-party: fetch.py builds it into
# build/<arch>/, and build.py packs it with ffmpeg.exe next to this DLL.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# the static CRT, like every plugin here: no VC++ redistributable needed
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(AVS_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk" CACHE PATH "AVS4YOU plugin SDK")

add_library(AiSubtitles SHARED
  src/dllmain.cpp
  src/export_utils.cpp
  src/exports.cpp
  src/plugin.cpp
  src/subtitles.cpp
  src/ui.cpp
  "${AVS_SDK_ROOT}/common/utils.cpp"
  "${AVS_SDK_ROOT}/common/ChildProcess.cpp"
  "${AVS_SDK_ROOT}/common/CpuFeatures.cpp"
  "${AVS_SDK_ROOT}/common/HttpDownload.cpp"
  "${AVS_SDK_ROOT}/translate/translate.cpp"
  "${AVS_SDK_ROOT}/ui/winapi/ui.cpp"
  AiSubtitles.rc
  module.def)
target_include_directories(AiSubtitles PRIVATE "${AVS_SDK_ROOT}/include")
target_compile_definitions(AiSubtitles PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_compile_options(AiSubtitles PRIVATE /utf-8)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set_property(SOURCE AiSubtitles.rc APPEND PROPERTY COMPILE_DEFINITIONS "RC_X64")
  set(AI_SUBTITLES_ARCH x64)
else()
  set(AI_SUBTITLES_ARCH x86)
endif()
target_link_libraries(AiSubtitles PRIVATE comctl32 comdlg32 shell32 winhttp)

# next to the tools fetch.py built, which is what the package is made of
set_target_properties(AiSubtitles PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/build/${AI_SUBTITLES_ARCH}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/build/${AI_SUBTITLES_ARCH}/debug")

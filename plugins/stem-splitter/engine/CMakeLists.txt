cmake_minimum_required(VERSION 3.20)
project(StemsEngine LANGUAGES CXX)

# stems.exe: Demucs v4 exported to ONNX, run by ONNX Runtime, with the STFT
# and model glue of demucs.onnx (MIT) and our own front end. fetch.py points
# this at a demucs.onnx checkout and the ONNX Runtime NuGet package.
# ONNX Runtime picks its CPU kernels at run time, so one build serves every CPU.

set(DEMUCS_ONNX_DIR "" CACHE PATH "demucs.onnx checkout")
set(ORT_DIR "" CACHE PATH "Microsoft.ML.OnnxRuntime NuGet package")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# the static CRT: stems.exe needs only onnxruntime.dll next to it
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ORT_RID win-x64)
else()
  set(ORT_RID win-x86)
endif()

# demucs.onnx includes <onnxruntime/core/session/onnxruntime_cxx_api.h>, the
# layout of an ONNX Runtime source tree; the NuGet package has the headers flat
set(ORT_SHIM "${CMAKE_CURRENT_BINARY_DIR}/ort_shim/onnxruntime/core/session")
file(MAKE_DIRECTORY "${ORT_SHIM}")
file(WRITE "${ORT_SHIM}/onnxruntime_cxx_api.h" "#pragma once\n#include <onnxruntime_cxx_api.h>\n")

add_executable(stems
  stems_cli.cpp
  separate.cpp
  "${DEMUCS_ONNX_DIR}/src/dsp.cpp"
  "${DEMUCS_ONNX_DIR}/src/model_inference.cpp")
target_include_directories(stems PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${DEMUCS_ONNX_DIR}/src"
  "${DEMUCS_ONNX_DIR}/vendor/eigen"
  "${ORT_DIR}/build/native/include"
  "${CMAKE_CURRENT_BINARY_DIR}/ort_shim")
target_compile_definitions(stems PRIVATE NDEBUG _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS
  EIGEN_DONT_PARALLELIZE)
target_compile_options(stems PRIVATE /O2 /fp:fast /bigobj /EHsc /W1)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  target_link_options(stems PRIVATE /LARGEADDRESSAWARE)
endif()
target_link_libraries(stems PRIVATE "${ORT_DIR}/runtimes/${ORT_RID}/native/onnxruntime.lib")

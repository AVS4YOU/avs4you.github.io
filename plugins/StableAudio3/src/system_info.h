#pragma once
#include <filesystem>
#include <string>

std::wstring GetSystemSummary(const std::filesystem::path &workDirectory);

// What sa3-cpp-cpu is built for: AVX2 with FMA and F16C, and an OS that saves
// the AVX registers. Without it only sa3-cpp-cpu-compat runs.
bool CpuHasAvx2();

// The CUDA backend can run here: x64 build, an NVIDIA driver with a CUDA
// device, the sa3-cpp-gpu files, a CUDA 12 runtime and an AVX2 CPU (its
// ggml-cpu.dll is the AVX2 one).
bool GpuBackendReady(const std::filesystem::path &moduleDirectory);

// The backend folder next to StableAudio3.dll a generation runs on: the GPU
// one when asked for and ready, otherwise the CPU one this processor can run.
std::wstring BackendFolder(const std::filesystem::path &moduleDirectory, bool gpu);

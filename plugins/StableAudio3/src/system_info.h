#pragma once
#include <filesystem>
#include <string>

std::wstring GetSystemSummary(const std::filesystem::path &workDirectory);

// What sa3-cpp-cpu is built for: AVX2 with FMA and F16C, and an OS that saves
// the AVX registers. Without it only sa3-cpp-cpu-compat runs.
bool CpuHasAvx2();

// The Vulkan backend can run here: x64 build, the sa3-cpp-gpu file, a GPU
// driver with a Vulkan 1.2 discrete or integrated GPU, and an AVX2 CPU (the
// backend's CPU code is the AVX2 one).
bool GpuBackendReady(const std::filesystem::path &moduleDirectory);

// Ready, and with a discrete GPU, which ggml-vulkan picks first: worth
// preselecting. An integrated GPU alone is offered but not preselected - it
// may not beat the processor.
bool GpuBackendPreferred(const std::filesystem::path &moduleDirectory);

// The backend folder next to StableAudio3.dll a generation runs on: the GPU
// one when asked for and ready, otherwise the CPU one this processor can run.
std::wstring BackendFolder(const std::filesystem::path &moduleDirectory, bool gpu);

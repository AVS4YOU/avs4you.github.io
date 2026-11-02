#include "system_info.h"
#include <algorithm>
#include <cwctype>
#include <dxgi.h>
#include <filesystem>
#include <immintrin.h>
#include <intrin.h>
#include <set>
#include <sstream>
#include <vector>
#include <windows.h>
#pragma comment(lib, "dxgi.lib")

namespace {
	bool HasMatchingDll(const std::filesystem::path& directory,
		const std::wstring& prefix) {
		std::error_code error;
		for (const auto& entry :
			std::filesystem::directory_iterator(directory, error)) {
			if (!entry.is_regular_file(error))
				continue;
			std::wstring name = entry.path().filename().wstring();
			std::transform(name.begin(), name.end(), name.begin(),
				[](wchar_t value) { return std::towlower(value); });
			if (name.rfind(prefix, 0) == 0 && entry.path().extension() == L".dll")
				return true;
		}
		return false;
	}

	std::wstring Join(const std::vector<std::wstring>& items) {
		std::wostringstream output;
		for (size_t index = 0; index < items.size(); ++index) {
			if (index)
				output << L", ";
			output << items[index];
		}
		return output.str();
	}

	struct CudaDriverStatus {
		bool ready = false;
		int version = 0;
		int devices = 0;
		std::wstring error;
	};

	bool CanLoadRuntimeLibrary(const wchar_t* name) {
		HMODULE library = LoadLibraryW(name);
		if (!library)
			return false;
		FreeLibrary(library);
		return true;
	}

	CudaDriverStatus CheckCudaDriver() {
		CudaDriverStatus status;
		HMODULE driver =
			LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!driver) {
			status.error = L"NVIDIA CUDA driver is missing";
			return status;
		}

		using CuInit = int(__stdcall*)(unsigned int);
		using CuDeviceGetCount = int(__stdcall*)(int*);
		using CuDriverGetVersion = int(__stdcall*)(int*);
		auto cuInit = reinterpret_cast<CuInit>(GetProcAddress(driver, "cuInit"));
		auto cuDeviceGetCount = reinterpret_cast<CuDeviceGetCount>(
			GetProcAddress(driver, "cuDeviceGetCount"));
		auto cuDriverGetVersion = reinterpret_cast<CuDriverGetVersion>(
			GetProcAddress(driver, "cuDriverGetVersion"));

		if (!cuInit || !cuDeviceGetCount || !cuDriverGetVersion)
			status.error = L"NVIDIA CUDA driver API is incomplete";
		else if (const int result = cuInit(0); result != 0)
			status.error =
			L"CUDA initialization failed (error " + std::to_wstring(result) + L")";
		else if (const int result = cuDeviceGetCount(&status.devices); result != 0)
			status.error =
			L"CUDA device query failed (error " + std::to_wstring(result) + L")";
		else if (status.devices == 0)
			status.error = L"no CUDA-capable GPU found";
		else {
			cuDriverGetVersion(&status.version);
			status.ready = true;
		}

		FreeLibrary(driver);
		return status;
	}

	struct GpuBackendStatus {
		CudaDriverStatus cuda;
		std::vector<std::wstring> missing;
		bool runtimeBundled = false;
		bool runtimeInstalled = false;
		bool avx2 = false;

		bool Ready() const {
			return cuda.ready && missing.empty() && (runtimeBundled || runtimeInstalled) &&
				avx2;
		}
	};

	GpuBackendStatus CheckGpuBackend(const std::filesystem::path& moduleDirectory) {
		GpuBackendStatus status;
		status.cuda = CheckCudaDriver();
		const auto gpuDirectory = moduleDirectory / L"sa3-cpp-gpu";
		std::error_code error;
		for (const wchar_t* name : { L"sa3.dll", L"ggml.dll", L"ggml-base.dll",
									L"ggml-cpu.dll", L"ggml-cuda.dll" })
			if (!std::filesystem::is_regular_file(gpuDirectory / name, error))
				status.missing.emplace_back(name);
		status.runtimeBundled = HasMatchingDll(gpuDirectory, L"cudart64_") &&
			HasMatchingDll(gpuDirectory, L"cublas64_") &&
			HasMatchingDll(gpuDirectory, L"cublaslt64_");
		status.runtimeInstalled = CanLoadRuntimeLibrary(L"cudart64_12.dll") &&
			CanLoadRuntimeLibrary(L"cublasLt64_12.dll") &&
			CanLoadRuntimeLibrary(L"cublas64_12.dll");
		status.avx2 = CpuHasAvx2();
		return status;
	}
} // namespace

bool CpuHasAvx2() {
	int regs[4]{};
	__cpuid(regs, 0);
	if (regs[0] < 7)
		return false;
	__cpuid(regs, 1);
	const int ecx = regs[2];
	// OSXSAVE, and the OS saving the XMM and YMM registers on a context switch
	if (!(ecx & (1 << 27)) || (_xgetbv(0) & 6) != 6)
		return false;
	// AVX, FMA, F16C: ggml's AVX2 build uses all three
	if (!(ecx & (1 << 28)) || !(ecx & (1 << 12)) || !(ecx & (1 << 29)))
		return false;
	__cpuidex(regs, 7, 0);
	return (regs[1] & (1 << 5)) != 0;
}

bool GpuBackendReady(const std::filesystem::path& moduleDirectory) {
#if defined(_WIN32) && !defined(_WIN64)
	(void)moduleDirectory;
	return false;
#else
	return CheckGpuBackend(moduleDirectory).Ready();
#endif
}

std::wstring BackendFolder(const std::filesystem::path& moduleDirectory, bool gpu) {
	if (gpu && GpuBackendReady(moduleDirectory))
		return L"sa3-cpp-gpu";
	// a package from before sa3-cpp-cpu-compat only has the AVX2 build
	std::error_code error;
	if (!CpuHasAvx2() &&
		std::filesystem::is_regular_file(moduleDirectory / L"sa3-cpp-cpu-compat" / L"sa3.dll",
			error))
		return L"sa3-cpp-cpu-compat";
	return L"sa3-cpp-cpu";
}

std::wstring GetSystemSummary(const std::filesystem::path& workDirectory) {
	SYSTEM_INFO si{};
	GetNativeSystemInfo(&si);
	MEMORYSTATUSEX ms{ sizeof(ms) };
	GlobalMemoryStatusEx(&ms);
	const bool avx2 = CpuHasAvx2();
	std::wostringstream output;
	output << L"CPU: " << si.dwNumberOfProcessors << L" logical threads, AVX2 "
		<< (avx2 ? L"yes" : L"no - compatible CPU backend") << L"\r\nRAM: " << (ms.ullAvailPhys >> 30)
		<< L" GB free / " << (ms.ullTotalPhys >> 30) << L" GB";

#if defined(_WIN32) && !defined(_WIN64)
	output << L"\r\nGPU Support: Not supported in the current version of the "
		L"plugin.";
#else
	IDXGIFactory1* factory = nullptr;
	if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
		reinterpret_cast<void**>(&factory)))) {
		IDXGIAdapter1* adapter = nullptr;
		std::set<std::wstring> displayedAdapters;
		for (UINT index = 0;
			factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND;
			++index) {
			DXGI_ADAPTER_DESC1 description{};
			adapter->GetDesc1(&description);
			std::wostringstream adapterKey;
			adapterKey << description.Description << L'|' << description.VendorId
				<< L'|' << description.DeviceId << L'|' << description.SubSysId
				<< L'|' << description.Revision << L'|'
				<< description.DedicatedVideoMemory;
			if (!(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
				displayedAdapters.insert(adapterKey.str()).second)
				output << L"\r\nGPU: " << description.Description << L", "
				<< (description.DedicatedVideoMemory >> 20) << L" MB VRAM";
			adapter->Release();
		}
		factory->Release();
	}

	const GpuBackendStatus gpu = CheckGpuBackend(workDirectory);
	const CudaDriverStatus& cuda = gpu.cuda;
	if (cuda.ready)
		output << L"\r\nCUDA: driver API " << cuda.version / 1000 << L"."
		<< (cuda.version % 1000) / 10 << L", " << cuda.devices
		<< L" device(s)";
	else
		output << L"\r\nCUDA: unavailable - " << cuda.error;

	if (gpu.Ready())
		output << L"\r\nGPU backend: ready (CUDA runtime: "
		<< (gpu.runtimeBundled ? L"bundled" : L"system") << L")";
	else {
		output << L"\r\nGPU backend: not ready";
		if (!gpu.missing.empty())
			output << L" - missing " << Join(gpu.missing);
		else if (cuda.ready && !gpu.runtimeBundled && !gpu.runtimeInstalled)
			output << L" - install NVIDIA CUDA Toolkit 12.9 "
			L"(CUDA Runtime and cuBLAS) to use GPU";
		else if (cuda.ready && !gpu.avx2)
			output << L" - needs a CPU with AVX2";
	}
#endif
	return output.str();
}

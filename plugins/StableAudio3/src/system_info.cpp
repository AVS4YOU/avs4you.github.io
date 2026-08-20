#include "system_info.h"
#include <algorithm>
#include <cwctype>
#include <dxgi.h>
#include <filesystem>
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
} // namespace

std::wstring GetSystemSummary(const std::filesystem::path& workDirectory) {
	SYSTEM_INFO si{};
	GetNativeSystemInfo(&si);
	MEMORYSTATUSEX ms{ sizeof(ms) };
	GlobalMemoryStatusEx(&ms);
	int regs[4]{};
	__cpuidex(regs, 7, 0);
	bool avx2 = (regs[1] & (1 << 5)) != 0;
	std::wostringstream output;
	output << L"CPU: " << si.dwNumberOfProcessors << L" logical threads, AVX2 "
		<< (avx2 ? L"yes" : L"no") << L"\r\nRAM: " << (ms.ullAvailPhys >> 30)
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

	const CudaDriverStatus cuda = CheckCudaDriver();
	if (cuda.ready)
		output << L"\r\nCUDA: driver API " << cuda.version / 1000 << L"."
		<< (cuda.version % 1000) / 10 << L", " << cuda.devices
		<< L" device(s)";
	else
		output << L"\r\nCUDA: unavailable - " << cuda.error;

	const auto gpuDirectory = workDirectory / L"sa3-cpp-gpu";
	std::vector<std::wstring> missing;
	std::error_code error;
	for (const wchar_t* name : { L"sa3.dll", L"ggml.dll", L"ggml-base.dll",
								L"ggml-cpu.dll", L"ggml-cuda.dll" })
		if (!std::filesystem::is_regular_file(gpuDirectory / name, error))
			missing.emplace_back(name);
	const bool runtimeBundled = HasMatchingDll(gpuDirectory, L"cudart64_") &&
		HasMatchingDll(gpuDirectory, L"cublas64_") &&
		HasMatchingDll(gpuDirectory, L"cublaslt64_");
	const bool runtimeInstalled = CanLoadRuntimeLibrary(L"cudart64_12.dll") &&
		CanLoadRuntimeLibrary(L"cublasLt64_12.dll") &&
		CanLoadRuntimeLibrary(L"cublas64_12.dll");

	if (cuda.ready && missing.empty() && (runtimeBundled || runtimeInstalled))
		output << L"\r\nGPU backend: ready (CUDA runtime: "
		<< (runtimeBundled ? L"bundled" : L"system") << L")";
	else {
		output << L"\r\nGPU backend: not ready";
		if (!missing.empty())
			output << L" - missing " << Join(missing);
		else if (cuda.ready && !runtimeBundled && !runtimeInstalled)
			output << L" - install NVIDIA CUDA Toolkit 12.9 "
			L"(CUDA Runtime and cuBLAS) to use GPU";
	}
#endif
	return output.str();
}

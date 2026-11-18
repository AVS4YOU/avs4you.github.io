#include "system_info.h"
#include <cstdint>
#include <cstring>
#include <dxgi.h>
#include <filesystem>
#include <immintrin.h>
#include <intrin.h>
#include <mutex>
#include <set>
#include <sstream>
#include <vector>
#include <windows.h>
#pragma comment(lib, "dxgi.lib")

namespace {
	// The few Vulkan declarations the probe needs, so StableAudio3.dll builds
	// without the Vulkan SDK; the Vulkan ABI fixes these layouts.
	namespace vk {
		using Result = int32_t; // VK_SUCCESS = 0
		struct InstanceT;
		struct PhysicalDeviceT;
		using Instance = InstanceT*;
		using PhysicalDevice = PhysicalDeviceT*;
		struct ApplicationInfo {
			int32_t sType = 0; // VK_STRUCTURE_TYPE_APPLICATION_INFO
			const void* pNext = nullptr;
			const char* pApplicationName = nullptr;
			uint32_t applicationVersion = 0;
			const char* pEngineName = nullptr;
			uint32_t engineVersion = 0;
			uint32_t apiVersion = 0;
		};
		struct InstanceCreateInfo {
			int32_t sType = 1; // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
			const void* pNext = nullptr;
			uint32_t flags = 0;
			const ApplicationInfo* pApplicationInfo = nullptr;
			uint32_t enabledLayerCount = 0;
			const char* const* ppEnabledLayerNames = nullptr;
			uint32_t enabledExtensionCount = 0;
			const char* const* ppEnabledExtensionNames = nullptr;
		};
		// VkPhysicalDeviceProperties as far as deviceName
		struct PhysicalDevicePropertiesHead {
			uint32_t apiVersion, driverVersion, vendorID, deviceID;
			int32_t deviceType;
			char deviceName[256];
		};
		constexpr int32_t kIntegratedGpu = 1, kDiscreteGpu = 2;
		using VoidFunction = void(__stdcall*)();
		using GetInstanceProcAddr = VoidFunction(__stdcall*)(Instance, const char*);
		using EnumerateInstanceVersion = Result(__stdcall*)(uint32_t*);
		using CreateInstance = Result(__stdcall*)(const InstanceCreateInfo*, const void*, Instance*);
		using DestroyInstance = void(__stdcall*)(Instance, const void*);
		using EnumeratePhysicalDevices = Result(__stdcall*)(Instance, uint32_t*, PhysicalDevice*);
		using GetPhysicalDeviceProperties = void(__stdcall*)(PhysicalDevice, void*);
		constexpr uint32_t Version(uint32_t major, uint32_t minor) { return (major << 22) | (minor << 12); }
	} // namespace vk

	// ggml-vulkan creates a Vulkan 1.2 instance and wants 1.2 devices
	constexpr uint32_t kMinimumApi = vk::Version(1, 2);
	constexpr uint32_t kMaxDevices = 8;

	struct ProbedDevice {
		char name[256];
		int32_t type;
		uint32_t api;
	};
	struct VulkanProbe {
		uint32_t instanceApi = 0;
		uint32_t count = 0;
		ProbedDevice devices[kMaxDevices]{};
		const wchar_t* error = nullptr;
	};

	// No C++ objects with destructors here: __try needs a plain frame. A broken
	// driver that faults while probing costs the GPU option, not the host app.
	void ProbeVulkanUnguarded(vk::GetInstanceProcAddr getProc, VulkanProbe& probe) {
		auto enumerateVersion = reinterpret_cast<vk::EnumerateInstanceVersion>(
			getProc(nullptr, "vkEnumerateInstanceVersion"));
		auto createInstance =
			reinterpret_cast<vk::CreateInstance>(getProc(nullptr, "vkCreateInstance"));
		if (!enumerateVersion || !createInstance ||
			enumerateVersion(&probe.instanceApi) != 0 || probe.instanceApi < kMinimumApi) {
			probe.error = L"the Vulkan runtime is older than 1.2";
			return;
		}
		vk::ApplicationInfo application;
		application.pApplicationName = "StableAudio3";
		application.apiVersion = kMinimumApi;
		vk::InstanceCreateInfo info;
		info.pApplicationInfo = &application;
		vk::Instance instance = nullptr;
		if (createInstance(&info, nullptr, &instance) != 0 || !instance) {
			probe.error = L"cannot create a Vulkan instance";
			return;
		}
		auto destroyInstance =
			reinterpret_cast<vk::DestroyInstance>(getProc(instance, "vkDestroyInstance"));
		auto enumerateDevices = reinterpret_cast<vk::EnumeratePhysicalDevices>(
			getProc(instance, "vkEnumeratePhysicalDevices"));
		auto getProperties = reinterpret_cast<vk::GetPhysicalDeviceProperties>(
			getProc(instance, "vkGetPhysicalDeviceProperties"));
		vk::PhysicalDevice handles[kMaxDevices]{};
		uint32_t count = kMaxDevices;
		const vk::Result listed = enumerateDevices && getProperties
			? enumerateDevices(instance, &count, handles) : -1;
		// VK_INCOMPLETE (5): more devices than kMaxDevices, the first ones will do
		if (listed == 0 || listed == 5) {
			for (uint32_t index = 0; index < count && index < kMaxDevices; ++index) {
				// the whole VkPhysicalDeviceProperties is about 820 bytes
				alignas(8) unsigned char properties[2048]{};
				getProperties(handles[index], properties);
				const auto* head =
					reinterpret_cast<const vk::PhysicalDevicePropertiesHead*>(properties);
				ProbedDevice& device = probe.devices[probe.count++];
				memcpy(device.name, head->deviceName, sizeof(device.name));
				device.name[sizeof(device.name) - 1] = 0;
				device.type = head->deviceType;
				device.api = head->apiVersion;
			}
		}
		else
			probe.error = L"cannot list the Vulkan devices";
		if (destroyInstance)
			destroyInstance(instance, nullptr);
	}

	void ProbeVulkanGuarded(vk::GetInstanceProcAddr getProc, VulkanProbe& probe) {
		__try {
			ProbeVulkanUnguarded(getProc, probe);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			probe.count = 0;
			probe.error = L"the Vulkan driver failed while being queried";
		}
	}

	// Once per process: the answer does not change while the host runs, and
	// each probe loads the drivers.
	const VulkanProbe& Vulkan() {
		static VulkanProbe probe;
		static std::once_flag once;
		std::call_once(once, [] {
			// the loader Windows installs with every GPU driver, never one
			// that happens to lie next to the host
			HMODULE loader = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (!loader) {
				probe.error = L"no Vulkan driver (vulkan-1.dll)";
				return;
			}
			auto getProc = reinterpret_cast<vk::GetInstanceProcAddr>(
				GetProcAddress(loader, "vkGetInstanceProcAddr"));
			if (getProc)
				ProbeVulkanGuarded(getProc, probe);
			else
				probe.error = L"vulkan-1.dll has no vkGetInstanceProcAddr";
			// kept loaded: the GPU backend's sa3.dll needs it next anyway
		});
		return probe;
	}

	bool Usable(const ProbedDevice& device) {
		return (device.type == vk::kDiscreteGpu || device.type == vk::kIntegratedGpu) &&
			device.api >= kMinimumApi;
	}

	struct GpuBackendStatus {
		bool usableGpu = false;
		bool discreteGpu = false;
		bool backendFile = false;
		bool avx2 = false;

		bool Ready() const { return usableGpu && backendFile && avx2; }
	};

	GpuBackendStatus CheckGpuBackend(const std::filesystem::path& moduleDirectory) {
		GpuBackendStatus status;
		const VulkanProbe& vulkan = Vulkan();
		for (uint32_t index = 0; index < vulkan.count; ++index)
			if (Usable(vulkan.devices[index])) {
				status.usableGpu = true;
				status.discreteGpu |= vulkan.devices[index].type == vk::kDiscreteGpu;
			}
		std::error_code error;
		status.backendFile =
			std::filesystem::is_regular_file(moduleDirectory / L"sa3-cpp-gpu" / L"sa3.dll", error);
		// sa3-cpp-gpu carries the AVX2 CPU code for what stays on the processor
		status.avx2 = CpuHasAvx2();
		return status;
	}

	std::wstring Widen(const char* text) {
		const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
		std::wstring result(size > 0 ? size - 1 : 0, L'\0');
		if (size > 1)
			MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), size);
		return result;
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

bool GpuBackendPreferred(const std::filesystem::path& moduleDirectory) {
#if defined(_WIN32) && !defined(_WIN64)
	(void)moduleDirectory;
	return false;
#else
	const GpuBackendStatus status = CheckGpuBackend(moduleDirectory);
	return status.Ready() && status.discreteGpu;
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

	const VulkanProbe& vulkan = Vulkan();
	if (vulkan.count) {
		output << L"\r\nVulkan " << (vulkan.instanceApi >> 22) << L"."
			<< ((vulkan.instanceApi >> 12) & 0x3ff) << L":";
		for (uint32_t index = 0; index < vulkan.count; ++index) {
			const ProbedDevice& device = vulkan.devices[index];
			output << (index ? L"," : L"") << L" " << Widen(device.name)
				<< (device.type == vk::kDiscreteGpu ? L" (discrete)"
					: device.type == vk::kIntegratedGpu ? L" (integrated)" : L" (not a GPU)");
		}
	}
	else
		output << L"\r\nVulkan: unavailable - " << (vulkan.error ? vulkan.error : L"no devices");

	const GpuBackendStatus gpu = CheckGpuBackend(workDirectory);
	if (gpu.Ready())
		output << L"\r\nGPU backend: ready (Vulkan)";
	else {
		output << L"\r\nGPU backend: not ready";
		if (!gpu.backendFile)
			output << L" - missing sa3-cpp-gpu\\sa3.dll";
		else if (!gpu.usableGpu)
			output << L" - needs a GPU with Vulkan 1.2";
		else if (!gpu.avx2)
			output << L" - needs a CPU with AVX2";
	}
#endif
	return output.str();
}

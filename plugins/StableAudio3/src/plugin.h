#pragma once
#include <windows.h>
#define PLUGIN_EXPORTS
#include "../../../sdk/include/CContentPluginIntf.h"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

struct GenerationOptions {
	std::wstring prompt, negativePrompt;
#if defined(_WIN32) && !defined(_WIN64)
	std::wstring model = L"small-music";
#else
	std::wstring model = L"medium";
#endif
	std::wstring encoding = L"f32", device = L"cpu";
	std::wstring distShift = L"LogSNR";
	int frames = 128, steps = 8, threads = 0;
	long long seed = -1;
	float cfg = 1.0f, padding = 6.0f;
	bool keepModels = false;
};

class CStableAudio3Plugin {
public:
	CStableAudio3Plugin();
	~CStableAudio3Plugin();
	CStableAudio3Plugin(const CStableAudio3Plugin&) = delete;

	std::filesystem::path moduleDirectory;
	std::filesystem::path workDirectory;
	HWND window = nullptr;
	AsyncCallback callback = nullptr;
	void* callbackContext = nullptr;
	std::atomic_bool cancel{ false };
	std::atomic_bool busy{ false };
	std::thread worker;
	std::mutex workerMutex;
	std::filesystem::path lastOutput;
	void StartGeneration(GenerationOptions options);
	void StartDownload(std::wstring model, std::wstring encoding);
	void Cancel();
	void JoinWorker();
	void PostStatus(const std::wstring& text, int progress = -1,
		bool finished = false, bool success = false);
};

extern HMODULE g_module;

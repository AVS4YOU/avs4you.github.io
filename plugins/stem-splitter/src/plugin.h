#pragma once
#include <windows.h>
#define PLUGIN_EXPORTS
#include "../../../sdk/include/CContentPluginIntf.h"
#include "stems.h"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

class CStemSplitterPlugin {
public:
	CStemSplitterPlugin();
	~CStemSplitterPlugin();
	CStemSplitterPlugin(const CStemSplitterPlugin&) = delete;

	std::filesystem::path moduleDirectory;  // the DLL, ffmpeg.exe, stems.exe, onnxruntime.dll
	std::filesystem::path workDirectory;    // %LOCALAPPDATA%\avs_plugin_stemsplitter
	HWND window = nullptr;
	AsyncCallback callback = nullptr;
	void* callbackContext = nullptr;
	std::atomic_bool cancel{ false };
	std::atomic_bool busy{ false };

	// what the window remembers between sessions (settings.json)
	std::wstring model = L"vocals";
	std::wstring format = L"wav";
	bool addToProject = true;
	std::filesystem::path tempDirectory;  // empty: the default, workDirectory\tmp
	void LoadSettings();
	void SaveSettings() const;

	StemTools Tools() const;
	void Start(StemJob job);
	void Cancel();
	void JoinWorker();

private:
	std::thread worker;
	std::mutex workerMutex;
	void PostStatus(const std::wstring& text, int progress, bool finished, bool success);
};

extern HMODULE g_module;

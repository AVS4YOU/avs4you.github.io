#pragma once
#include <windows.h>
#define PLUGIN_EXPORTS
#include "../../../sdk/include/CContentPluginIntf.h"
#include "subtitles.h"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

class CSubtitlesPlugin {
public:
	CSubtitlesPlugin();
	~CSubtitlesPlugin();
	CSubtitlesPlugin(const CSubtitlesPlugin&) = delete;

	std::filesystem::path moduleDirectory;  // the DLL, ffmpeg.exe, whisper-cli*.exe
	std::filesystem::path workDirectory;    // %LOCALAPPDATA%\avs_plugin_aisubtitles
	HWND window = nullptr;
	AsyncCallback callback = nullptr;
	void* callbackContext = nullptr;
	std::atomic_bool cancel{ false };
	std::atomic_bool busy{ false };

	// what the window remembers between sessions (settings.json)
	std::wstring model = L"small";
	std::wstring language = L"auto";
	bool translate = false;
	bool speechOnly = true;  // "Speech" rather than "Music and songs"
	bool useGpu = true;
	void LoadSettings();
	void SaveSettings() const;

	SubtitleTools Tools() const;
	void Start(SubtitleJob job);
	void Cancel();
	void JoinWorker();

private:
	std::thread worker;
	std::mutex workerMutex;
	void PostStatus(const std::wstring& text, int progress, bool finished, bool success);
};

extern HMODULE g_module;

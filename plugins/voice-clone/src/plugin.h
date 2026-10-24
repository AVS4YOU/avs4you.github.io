#pragma once
#include <windows.h>
#define PLUGIN_EXPORTS
#include "../../../sdk/include/CContentPluginIntf.h"
#include "voice.h"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

class CVoiceClonePlugin {
public:
	CVoiceClonePlugin();
	~CVoiceClonePlugin();
	CVoiceClonePlugin(const CVoiceClonePlugin&) = delete;

	std::filesystem::path moduleDirectory;  // the DLL, ffmpeg.exe, seedvc\ (the engine)
	std::filesystem::path workDirectory;    // %LOCALAPPDATA%\avs_plugin_voiceclone
	HWND window = nullptr;
	AsyncCallback callback = nullptr;
	void* callbackContext = nullptr;
	std::atomic_bool cancel{ false };
	std::atomic_bool busy{ false };

	// what the window remembers between sessions (settings.json)
	std::filesystem::path voice;  // the last voice sample
	std::wstring quality = L"standard";
	std::wstring format = L"wav";
	int pitch = 0;
	bool matchVoicePitch = false;
	bool useGpu = true;
	bool addToProject = true;
	void LoadSettings();
	void SaveSettings() const;

	VoiceTools Tools() const;
	void Start(VoiceJob job);
	void Cancel();
	void JoinWorker();

private:
	std::thread worker;
	std::mutex workerMutex;
	void PostStatus(const std::wstring& text, int progress, bool finished, bool success);
};

extern HMODULE g_module;

#pragma once
#define PLUGIN_EXPORTS
#include "../../../sdk/include/CContentPluginIntf.h"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

constexpr UINT WM_SUPERTONIC_STATUS = WM_APP + 301;
struct UiStatus { std::wstring text; int progress; bool finished; bool success; bool outputReady; };
struct GenerationOptions {
    std::wstring text;
    std::wstring language = L"ru";
    std::filesystem::path voiceStyle;
    int steps = 8;
    float speed = 1.05f;
};

class SupertonicPlugin {
public:
    SupertonicPlugin();
    ~SupertonicPlugin();
    std::filesystem::path moduleDirectory, workDirectory, modelDirectory, lastOutput;
    std::vector<std::filesystem::path> voiceStyles;
    HWND window = nullptr;
    HWND parentWindow = nullptr;
    HANDLE activationContext = INVALID_HANDLE_VALUE;
    ULONG_PTR activationCookie = 0;
    DWORD activationThreadId = 0;
    AsyncCallback callback = nullptr;
    void* callbackContext = nullptr;
    std::atomic_bool busy{false};
    std::thread worker;
    std::mutex workerMutex;
    void Start(GenerationOptions options);
    void StartDownload();
    void Join();
    void RefreshModelState();
    void Post(std::wstring text, int progress = -1, bool finished = false, bool success = false, bool outputReady = false);
};

extern HMODULE g_module;
bool GenerateSpeech(SupertonicPlugin& plugin, const GenerationOptions& options, std::wstring& error);
void ShowSupertonicWindow(SupertonicPlugin* plugin);






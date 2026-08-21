#include "pch.h"
#include "plugin.h"
#include "resource.h"
#include "../../../sdk/translate/translate.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <shlobj.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace
{
std::wstring Translate(const wchar_t* text)
{
    auto* manager = CTranslate::GetInstance().GetManager();
    return manager ? manager->Translate(text) : std::wstring(text);
}

std::filesystem::path ModuleDirectory()
{
    wchar_t path[32768]{};
    GetModuleFileNameW(g_module, path, static_cast<DWORD>(std::size(path)));
    return std::filesystem::path(path).parent_path();
}

bool IsModelRoot(const std::filesystem::path& path)
{
    return std::filesystem::exists(path / L"onnx" / L"tts.json") &&
           std::filesystem::exists(path / L"onnx" / L"unicode_indexer.json") &&
           std::filesystem::exists(path / L"onnx" / L"duration_predictor.onnx") &&
           std::filesystem::exists(path / L"onnx" / L"text_encoder.onnx") &&
           std::filesystem::exists(path / L"onnx" / L"vector_estimator.onnx") &&
           std::filesystem::exists(path / L"onnx" / L"vocoder.onnx") &&
           std::filesystem::exists(path / L"voice_styles");
}

struct InternetHandle
{
    HINTERNET value = nullptr;
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
};

bool DownloadFile(HINTERNET connection, const std::wstring& remotePath,
                  const std::filesystem::path& destination,
                  const std::function<void(std::uint64_t, std::uint64_t)>& onProgress,
                  std::wstring& errorText)
{
    InternetHandle request{WinHttpOpenRequest(connection, L"GET", remotePath.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.value || !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr))
    {
        errorText = Translate(L"Unable to connect to Hugging Face. Error ") + std::to_wstring(GetLastError()) + L".";
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (statusCode < 200 || statusCode >= 300)
    {
        errorText = Translate(L"Model server returned HTTP ") + std::to_wstring(statusCode) + L".";
        return false;
    }

    std::uint64_t contentLength = 0;
    wchar_t lengthText[64]{};
    DWORD lengthSize = sizeof(lengthText);
    if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH,
            WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &lengthSize, WINHTTP_NO_HEADER_INDEX))
    {
        try { contentLength = std::stoull(lengthText); }
        catch (...) { contentLength = 0; }
    }

    std::error_code fileError;
    std::filesystem::create_directories(destination.parent_path(), fileError);
    const std::filesystem::path temporary(destination.wstring() + L".part");
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (fileError || !output)
    {
        errorText = Translate(L"Unable to create ") + destination.wstring() + L".";
        return false;
    }

    std::vector<char> buffer(1024 * 1024);
    std::uint64_t receivedTotal = 0;
    while (true)
    {
        DWORD received = 0;
        if (!WinHttpReadData(request.value, buffer.data(),
                static_cast<DWORD>(buffer.size()), &received))
        {
            errorText = Translate(L"Model download failed. Error ") + std::to_wstring(GetLastError()) + L".";
            output.close();
            std::filesystem::remove(temporary, fileError);
            return false;
        }
        if (received == 0)
            break;
        output.write(buffer.data(), received);
        if (!output)
        {
            errorText = Translate(L"Unable to write ") + destination.wstring() + L".";
            output.close();
            std::filesystem::remove(temporary, fileError);
            return false;
        }
        receivedTotal += received;
        onProgress(receivedTotal, contentLength);
    }
    output.close();

    std::filesystem::remove(destination, fileError);
    fileError.clear();
    std::filesystem::rename(temporary, destination, fileError);
    if (fileError)
    {
        std::filesystem::remove(temporary, fileError);
        errorText = Translate(L"Unable to install ") + destination.filename().wstring() + L".";
        return false;
    }
    return true;
}
}

SupertonicPlugin::SupertonicPlugin() : moduleDirectory(ModuleDirectory())
{
    wchar_t localAppData[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData)))
        workDirectory = std::filesystem::path(localAppData) / L"avs_plugin_supertonic3";
    else
        workDirectory = std::filesystem::temp_directory_path() / L"avs_plugin_supertonic3";

    std::error_code error;
    std::filesystem::create_directories(workDirectory, error);

    const auto internalIcon = workDirectory / L"icon_internal.ico";
    const auto applicationIcon = workDirectory / L"icon.ico";
    const HRSRC resource = FindResourceW(g_module, MAKEINTRESOURCEW(IDR_ICON), RT_RCDATA);
    if (resource)
    {
        const DWORD size = SizeofResource(g_module, resource);
        const HGLOBAL loaded = LoadResource(g_module, resource);
        const void* data = loaded ? LockResource(loaded) : nullptr;
        if (data && size)
        {
            std::ofstream iconFile(internalIcon, std::ios::binary | std::ios::trunc);
            iconFile.write(static_cast<const char*>(data), size);
        }
    }
    CopyFileW(internalIcon.c_str(), applicationIcon.c_str(), FALSE);
    RefreshModelState();
}

SupertonicPlugin::~SupertonicPlugin() { Join(); }

void SupertonicPlugin::RefreshModelState()
{
    modelDirectory.clear();
    voiceStyles.clear();
    const auto localModel = workDirectory / L"models";
    if (!IsModelRoot(localModel))
        return;

    modelDirectory = localModel;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(localModel / L"voice_styles", error))
        if (entry.is_regular_file() && entry.path().extension() == L".json")
            voiceStyles.push_back(entry.path());
    std::sort(voiceStyles.begin(), voiceStyles.end());
}

void SupertonicPlugin::Join()
{
    std::lock_guard<std::mutex> lock(workerMutex);
    if (worker.joinable())
        worker.join();
}

void SupertonicPlugin::Post(std::wstring text, int progress, bool finished,
                            bool success, bool outputReady)
{
    if (window && IsWindow(window))
        PostMessageW(window, WM_SUPERTONIC_STATUS, 0,
            reinterpret_cast<LPARAM>(new UiStatus{
                std::move(text), progress, finished, success, outputReady}));
}

void SupertonicPlugin::StartDownload()
{
    if (busy.exchange(true))
        return;
    Join();
    worker = std::thread([this] {
        const std::vector<std::wstring> files{
            L"onnx/duration_predictor.onnx", L"onnx/text_encoder.onnx",
            L"onnx/tts.json", L"onnx/unicode_indexer.json",
            L"onnx/vector_estimator.onnx", L"onnx/vocoder.onnx",
            L"voice_styles/F1.json", L"voice_styles/F2.json", L"voice_styles/F3.json",
            L"voice_styles/F4.json", L"voice_styles/F5.json", L"voice_styles/M1.json",
            L"voice_styles/M2.json", L"voice_styles/M3.json", L"voice_styles/M4.json",
            L"voice_styles/M5.json"
        };

        InternetHandle session{WinHttpOpen(L"AVS Supertonic3 Plugin/1.0",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0)};
        InternetHandle connection;
        if (session.value)
            connection.value = WinHttpConnect(session.value, L"huggingface.co",
                INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!session.value || !connection.value)
        {
            busy = false;
            Post(Translate(L"Unable to connect to Hugging Face. Check your internet connection."), 0, true, false);
            return;
        }

        const auto destinationRoot = workDirectory / L"models";
        std::wstring errorText;
        int lastProgress = -1;
        bool downloaded = true;
        for (size_t index = 0; index < files.size(); ++index)
        {
            const auto& relative = files[index];
            const std::wstring remote = L"/Supertone/supertonic-3/resolve/main/" + relative + L"?download=true";
            const auto destination = destinationRoot / std::filesystem::path(relative);
            downloaded = DownloadFile(connection.value, remote, destination,
                [this, &files, index, &relative, &lastProgress](std::uint64_t received, std::uint64_t length) {
                    const int fileProgress = length > 0
                        ? static_cast<int>((received * 100) / length) : 0;
                    const int progress = static_cast<int>((index * 100 + fileProgress) / files.size());
                    if (progress != lastProgress)
                    {
                        lastProgress = progress;
                        Post(Translate(L"Downloading model: ") + relative, progress);
                    }
                }, errorText);
            if (!downloaded)
                break;
        }

        if (downloaded)
            RefreshModelState();
        const bool installed = downloaded && !modelDirectory.empty() && !voiceStyles.empty();
        busy = false;
        Post(installed ? Translate(L"Model download completed.") :
             (errorText.empty() ? Translate(L"Model download failed.") : errorText),
             installed ? 100 : 0, true, installed);
    });
}

void SupertonicPlugin::Start(GenerationOptions options)
{
    if (busy.exchange(true))
        return;
    Join();
    worker = std::thread([this, options = std::move(options)] {
        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        lastOutput = workDirectory /
            (L"supertonic3-" + std::to_wstring(stamp) + L".wav");
        std::wstring error;
        const bool ok = GenerateSpeech(*this, options, error);
        busy = false;
        Post(ok ? Translate(L"Generation completed.") : error, ok ? 100 : 0, true, ok, ok);
    });
}




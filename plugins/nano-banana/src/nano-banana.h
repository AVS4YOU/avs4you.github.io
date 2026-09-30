#pragma once
#include <chrono>
#include <codecvt>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include "nano-banana_utils.h"

class CNanoBanana
{
public:
    NSGenerationMode::GenerationMode m_generation_mode = NSGenerationMode::GenerationMode::TextToImage;
    std::vector<std::wstring> m_additional_files_paths = {};
    std::wstring m_prompt = L"Create a polished image for AVS4YOU media suite.";
    std::wstring m_aspectRatio = L"1:1";
    std::wstring m_imageSize = L"1K";
    std::wstring m_mimeType = L"image/jpeg";
    std::wstring m_model = L"gemini-3.1-flash-image";
    std::wstring m_file = L"";
    std::wstring m_key = L"";
    std::wstring m_previousInteractionId = L"";
    std::wstring m_videoUri = L"";

    NSProcesses::CProcessManager* m_manager = nullptr;

    CNanoBanana() = default;
    CNanoBanana(const CNanoBanana&) = delete;
    CNanoBanana(CNanoBanana&&) = delete;
    ~CNanoBanana();

    void Process(NSProcesses::CProcessRunnerCallback* callback, const std::wstring& workDirectory);
    std::wstring GetCurrentDateTime();
protected:
    std::wstring CreateWorkDirectory();
};

#pragma once
#include <chrono>
#include <codecvt>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include "veo3_utils.h"

const std::wstring SCRIPT = LR"()";

// Sends a request to the API and receives a response(file and progress) using CProcessManager and a PowerShell script
class CVeo3
{
public:
    std::wstring m_key = L"";
    std::wstring m_prompt = L"AVS - best media suite.";
    std::wstring m_seconds = L"4";
    std::wstring m_resolution = L"720p";
    std::wstring m_file = L"";
    std::wstring m_model = L"veo3";
    DWORD m_start_time;

    NSProcesses::CProcessManager* m_manager = nullptr;

    CVeo3() = default;
    CVeo3(const CVeo3&) = delete;
    CVeo3(CVeo3&&) = delete;
    ~CVeo3();

    void Process(NSProcesses::CProcessRunnerCallback* callback, const std::wstring& workDirectory);

    void FakeStart();
    int GetFakeProgress();

protected:
    std::wstring CreateWorkDirectory();
    std::wstring GetCurrentDateTime();
};

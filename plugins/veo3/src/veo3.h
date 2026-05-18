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

const std::wstring SCRIPT = LR"([Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = "Stop"

$logfile = "${PARAM_VIDEO_LOG_FILE}"

function Write-Log($msg) {
    $timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    "$timestamp $msg" | Out-File -FilePath $logfile -Append -Encoding UTF8
}

function Write-Cmd($msg) {
    Write-Output ($msg -replace "`r`n", "" -replace "`n", "") + "`n"
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -Path $ScriptDir
$apiKey     = "${PARAM_KEY}"
$prompt     = Get-Content -Path "script.prompt" -Raw -Encoding UTF8
$promptText = [string]$prompt

$output      = "${PARAM_VIDEO_NAME}"
$model       = "${PARAM_MODEL_NAME}"
$resolution  = "${PARAM_RESOLUTION}"
$aspectRatio = "${PARAM_ASPECT_RATIO}"
$duration    = "${PARAM_SECONDS}"

$baseUrl = "https://generativelanguage.googleapis.com/v1beta"
$createUrl = "$baseUrl/models/$model`:predictLongRunning"

Write-Log "Creating Veo video job..."
Write-Log "Model: $model"
Write-Log "Resolution: $resolution"
Write-Log "AspectRatio: $aspectRatio"

$parameters = @{}

$duration = $duration.Trim()
$resolution = $resolution.Trim()
$aspectRatio = $aspectRatio.Trim()

$parameters.aspectRatio = $aspectRatio
$parameters.resolution = $resolution
$parameters.durationSeconds = [int]$duration

$bodyObject = @{
    instances = @(
        @{
            prompt = $promptText
        }
    )
    parameters = $parameters
}

$body = $bodyObject | ConvertTo-Json -Depth 10
$bytes = [System.Text.Encoding]::UTF8.GetBytes($body)

$headers = @{
    "x-goog-api-key" = $apiKey
    "Content-Type"   = "application/json"
}

# --- CREATE LONG-RUNNING JOB ---
try {
    $response = Invoke-RestMethod `
        -Method Post `
        -Uri $createUrl `
        -Headers $headers `
        -Body $bytes

    Write-Log "Create operation response:"
    Write-Log ($response | ConvertTo-Json -Depth 20)

    Write-Cmd ("[SUCCESS]" + ($response | ConvertTo-Json -Depth 20 -Compress))
}
catch {
    Write-Log "Failed to create Veo job: $_"
    $errorBody = ""

    try {
        $stream = $_.Exception.Response.GetResponseStream()
        $reader = New-Object System.IO.StreamReader($stream)
        $errorBody = $reader.ReadToEnd()
        $reader.Close()
    }
    catch {
        $errorBody = ""
    }

    if ($errorBody -and $errorBody.Length -gt 0) {
        Write-Log "Error body:"
        Write-Log $errorBody
        Write-Cmd ("[ERROR]" + $errorBody)
    }
    else {
        Write-Cmd "[ERROR]$_"
    }

    exit
}

$operationName = $response.name

if (-not $operationName) {
    Write-Log "Failed to get operation name:"
    Write-Log ($response | ConvertTo-Json -Depth 20)
    Write-Cmd ("[ERROR]" + ($response | ConvertTo-Json -Depth 20 -Compress))
    exit
}

Write-Log "Operation name: $operationName"

# --- POLL OPERATION STATUS ---
while ($true) {

    $statusUrl = "$baseUrl/$operationName"

    try {
        $statusResp = Invoke-RestMethod `
            -Method Get `
            -Uri $statusUrl `
            -Headers @{
                "x-goog-api-key" = $apiKey
            }

        Write-Log "Status response:"
        Write-Log ($statusResp | ConvertTo-Json -Depth 20)

        Write-Cmd ("[SUCCESS]" + ($statusResp | ConvertTo-Json -Depth 20 -Compress))
    }
    catch {
        Write-Log "Failed to query operation status: $_"
        Write-Cmd "[WARNING]$_"
        Start-Sleep -Seconds 10
        continue
    }

    if ($statusResp.error) {
        Write-Log "Veo generation failed:"
        Write-Log ($statusResp.error | ConvertTo-Json -Depth 20)
        Write-Cmd ("[ERROR]" + ($statusResp.error | ConvertTo-Json -Depth 20 -Compress))
        exit
    }

    if ($statusResp.done -eq $true) {
        Write-Log "Operation completed"
        break
    }

    Write-Log "Waiting for Veo generation..."
    Start-Sleep -Seconds 10
}

# --- EXTRACT VIDEO URI ---
$videoUri = $statusResp.response.generateVideoResponse.generatedSamples[0].video.uri

if (-not $videoUri) {
    Write-Log "Failed to get video URI:"
    Write-Log ($statusResp | ConvertTo-Json -Depth 20)
    Write-Cmd ("[ERROR]" + ($statusResp | ConvertTo-Json -Depth 20 -Compress))
    exit
}

Write-Log "Video URI: $videoUri"
Write-Log "Downloading video..."

# --- DOWNLOAD VIDEO ---
try {
    Invoke-WebRequest `
        -Uri $videoUri `
        -Headers @{
            "x-goog-api-key" = $apiKey
        } `
        -OutFile $output

    Write-Log "Download completed: $output"

    $result = @{
        file = $output
        videoUri = $videoUri
        operationName = $operationName
    }

    Write-Cmd ("[SUCCESS]" + ($result | ConvertTo-Json -Compress))
}
catch {
    Write-Log "Failed to download video: $_"
    Write-Cmd "[ERROR]$_"
    exit
})";

// Sends a request to the API and receives a response(file and progress) using CProcessManager and a PowerShell script
class CVeo3
{
public:
    std::vector<std::wstring> m_additional_files_paths = {};
    std::wstring m_prompt = L"AVS - best media suite.";
    std::wstring m_personGeneration = L"allow_all";
    std::wstring m_durationSeconds = L"4";
    std::wstring m_aspectRatio = L"16:9";
    std::wstring m_resolution = L"720p";
    std::wstring m_model = L"veo3";
    std::wstring m_file = L"";
    std::wstring m_key = L"";
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

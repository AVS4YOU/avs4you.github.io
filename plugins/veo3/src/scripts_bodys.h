#pragma once
#include <string>

const std::wstring SCRIPT_FIRST_AND_LAST_FRAME = LR"([Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = "Stop"

$logfile = "${PARAM_VIDEO_LOG_FILE}"

function Write-Log($msg) {
    $timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    "$timestamp $msg" | Out-File -FilePath $logfile -Append -Encoding UTF8
}

function Write-Cmd($msg) {
    Write-Output (($msg -replace "`r`n", "" -replace "`n", "") + "`n")
}

function Read-File-Base64($filePath) {
    $bytes = [System.IO.File]::ReadAllBytes($filePath)
    return [System.Convert]::ToBase64String($bytes)
}

function Get-Mime-Type($filePath) {
    $ext = [System.IO.Path]::GetExtension($filePath).ToLowerInvariant()

    switch ($ext) {
        ".png"  { return "image/png" }
        ".jpg"  { return "image/jpeg" }
        ".jpeg" { return "image/jpeg" }
        ".webp" { return "image/webp" }
        default { return "image/png" }
    }
}

function Read-Error-Body($err) {
    try {
        $stream = $err.Exception.Response.GetResponseStream()
        $reader = New-Object System.IO.StreamReader($stream)
        $text = $reader.ReadToEnd()
        $reader.Close()
        return $text
    }
    catch {
        return ""
    }
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -Path $ScriptDir

$apiKey      = "${PARAM_KEY}"
$prompt      = Get-Content -Path "script.prompt" -Raw -Encoding UTF8
$promptText  = [string]$prompt

$output      = "${PARAM_VIDEO_NAME}"
$model       = "${PARAM_MODEL_NAME}"
$resolution  = "${PARAM_RESOLUTION}".Trim()
$aspectRatio = "${PARAM_ASPECT_RATIO}".Trim()
$duration    = "${PARAM_SECONDS}".Trim()

# First frame and last frame images
$firstImagePath = "${PARAM_IMAGE_1}".Trim()
$lastImagePath  = "${PARAM_IMAGE_2}".Trim()

$baseUrl = "https://generativelanguage.googleapis.com/v1beta"
$createUrl = "$baseUrl/models/$model`:predictLongRunning"

Write-Log "Creating Veo video job with first frame and last frame..."
Write-Log "Model: $model"
Write-Log "Resolution: $resolution"
Write-Log "AspectRatio: $aspectRatio"
Write-Log "Duration: $duration"
Write-Log "First image: $firstImagePath"
Write-Log "Last image: $lastImagePath"

$parameters = @{
    aspectRatio     = $aspectRatio
    resolution      = $resolution
    durationSeconds = [int]$duration
    sampleCount     = 1
}

$firstImageBase64 = Read-File-Base64 $firstImagePath
$lastImageBase64  = Read-File-Base64 $lastImagePath

$firstMimeType = Get-Mime-Type $firstImagePath
$lastMimeType  = Get-Mime-Type $lastImagePath

$instance = @{
    prompt = $promptText

    image = @{
        bytesBase64Encoded = $firstImageBase64
        mimeType = $firstMimeType
    }

    lastFrame = @{
        bytesBase64Encoded = $lastImageBase64
        mimeType = $lastMimeType
    }
}

$bodyObject = @{
    instances  = @($instance)
    parameters = $parameters
}

$body = $bodyObject | ConvertTo-Json -Depth 30
$bytes = [System.Text.Encoding]::UTF8.GetBytes($body)

$headers = @{
    "x-goog-api-key" = $apiKey
    "Content-Type"   = "application/json"
}

Write-Log "Request URL:"
Write-Log $createUrl

Write-Log "Request body created"

# --- CREATE LONG-RUNNING JOB ---
try {
    $response = Invoke-RestMethod `
        -Method Post `
        -Uri $createUrl `
        -Headers $headers `
        -Body $bytes

    Write-Log "Create operation response:"
    Write-Log ($response | ConvertTo-Json -Depth 30)

    Write-Cmd ("[SUCCESS]" + ($response | ConvertTo-Json -Depth 30 -Compress))
}
catch {
    Write-Log "Failed to create Veo job: $_"

    $errorBody = Read-Error-Body $_

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
    Write-Log ($response | ConvertTo-Json -Depth 30)
    Write-Cmd ("[ERROR]" + ($response | ConvertTo-Json -Depth 30 -Compress))
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
        Write-Log ($statusResp | ConvertTo-Json -Depth 30)

        Write-Cmd ("[SUCCESS]" + ($statusResp | ConvertTo-Json -Depth 30 -Compress))
    }
    catch {
        Write-Log "Failed to query operation status: $_"

        $errorBody = Read-Error-Body $_

        if ($errorBody -and $errorBody.Length -gt 0) {
            Write-Log "Status error body:"
            Write-Log $errorBody
            Write-Cmd ("[WARNING]" + $errorBody)
        }
        else {
            Write-Cmd "[WARNING]$_"
        }

        Start-Sleep -Seconds 10
        continue
    }

    if ($statusResp.error) {
        Write-Log "Veo generation failed:"
        Write-Log ($statusResp.error | ConvertTo-Json -Depth 30)
        Write-Cmd ("[ERROR]" + ($statusResp.error | ConvertTo-Json -Depth 30 -Compress))
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
    Write-Log ($statusResp | ConvertTo-Json -Depth 30)
    Write-Cmd ("[ERROR]" + ($statusResp | ConvertTo-Json -Depth 30 -Compress))
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

    $errorBody = Read-Error-Body $_

    if ($errorBody -and $errorBody.Length -gt 0) {
        Write-Log "Download error body:"
        Write-Log $errorBody
        Write-Cmd ("[ERROR]" + $errorBody)
    }
    else {
        Write-Cmd "[ERROR]$_"
    }

    exit
})";

const std::wstring SCRIPT_IMAGE_TO_VIDEO = LR"([Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = "Stop"

$logfile = "${PARAM_VIDEO_LOG_FILE}"

function Write-Log($msg) {
    $timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    "$timestamp $msg" | Out-File -FilePath $logfile -Append -Encoding UTF8
}

function Write-Cmd($msg) {
    Write-Output (($msg -replace "`r`n", "" -replace "`n", "") + "`n")
}

function Read-File-Base64($filePath) {
    $bytes = [System.IO.File]::ReadAllBytes($filePath)
    return [System.Convert]::ToBase64String($bytes)
}

function Get-Mime-Type($filePath) {
    $ext = [System.IO.Path]::GetExtension($filePath).ToLowerInvariant()

    switch ($ext) {
        ".png"  { return "image/png" }
        ".jpg"  { return "image/jpeg" }
        ".jpeg" { return "image/jpeg" }
        ".webp" { return "image/webp" }
        default { return "image/png" }
    }
}

function Read-Error-Body($err) {
    try {
        $stream = $err.Exception.Response.GetResponseStream()
        $reader = New-Object System.IO.StreamReader($stream)
        $text = $reader.ReadToEnd()
        $reader.Close()
        return $text
    }
    catch {
        return ""
    }
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -Path $ScriptDir

$apiKey      = "${PARAM_KEY}"
$prompt      = Get-Content -Path "script.prompt" -Raw -Encoding UTF8
$promptText  = [string]$prompt

$output      = "${PARAM_VIDEO_NAME}"
$model       = "${PARAM_MODEL_NAME}"
$resolution  = "${PARAM_RESOLUTION}".Trim()
$aspectRatio = "${PARAM_ASPECT_RATIO}".Trim()
$duration    = "${PARAM_SECONDS}".Trim()

$imagePaths = @(
    "${PARAM_IMAGE_1}",
    "${PARAM_IMAGE_2}",
    "${PARAM_IMAGE_3}"
)

$baseUrl = "https://generativelanguage.googleapis.com/v1beta"
$createUrl = "$baseUrl/models/$model`:predictLongRunning"

Write-Log "Creating Veo video job with reference images..."
Write-Log "Model: $model"
Write-Log "Resolution: $resolution"
Write-Log "AspectRatio: $aspectRatio"
Write-Log "Duration: $duration"

$parameters = @{
    aspectRatio     = $aspectRatio
    resolution      = $resolution
    durationSeconds = [int]$duration
    sampleCount     = 1
}

$referenceImages = @()

foreach ($imagePath in $imagePaths) {
    $imagePath = $imagePath.Trim()

    if ($imagePath.Length -eq 0) {
        continue
    }

    $imageBase64 = Read-File-Base64 $imagePath
    $mimeType = Get-Mime-Type $imagePath

    $referenceImages += @{
        referenceType = "asset"
        image = @{
            bytesBase64Encoded = $imageBase64
            mimeType           = $mimeType
        }
    }
}

$instance = @{
    prompt = $promptText
}

if ($referenceImages.Count -gt 0) {
    $instance.referenceImages = @($referenceImages)
}

$bodyObject = @{
    instances  = @($instance)
    parameters = $parameters
}

$body = $bodyObject | ConvertTo-Json -Depth 30
$bytes = [System.Text.Encoding]::UTF8.GetBytes($body)

$headers = @{
    "x-goog-api-key" = $apiKey
    "Content-Type"   = "application/json"
}

Write-Log "Request URL:"
Write-Log $createUrl

# --- CREATE LONG-RUNNING JOB ---
try {
    $response = Invoke-RestMethod `
        -Method Post `
        -Uri $createUrl `
        -Headers $headers `
        -Body $bytes

    Write-Log "Create operation response:"
    Write-Log ($response | ConvertTo-Json -Depth 30)

    Write-Cmd ("[SUCCESS]" + ($response | ConvertTo-Json -Depth 30 -Compress))
}
catch {
    Write-Log "Failed to create Veo job: $_"

    $errorBody = Read-Error-Body $_

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
    Write-Log ($response | ConvertTo-Json -Depth 30)
    Write-Cmd ("[ERROR]" + ($response | ConvertTo-Json -Depth 30 -Compress))
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
        Write-Log ($statusResp | ConvertTo-Json -Depth 30)

        Write-Cmd ("[SUCCESS]" + ($statusResp | ConvertTo-Json -Depth 30 -Compress))
    }
    catch {
        Write-Log "Failed to query operation status: $_"

        $errorBody = Read-Error-Body $_

        if ($errorBody -and $errorBody.Length -gt 0) {
            Write-Log "Status error body:"
            Write-Log $errorBody
            Write-Cmd ("[WARNING]" + $errorBody)
        }
        else {
            Write-Cmd "[WARNING]$_"
        }

        Start-Sleep -Seconds 10
        continue
    }

    if ($statusResp.error) {
        Write-Log "Veo generation failed:"
        Write-Log ($statusResp.error | ConvertTo-Json -Depth 30)
        Write-Cmd ("[ERROR]" + ($statusResp.error | ConvertTo-Json -Depth 30 -Compress))
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
    Write-Log ($statusResp | ConvertTo-Json -Depth 30)
    Write-Cmd ("[ERROR]" + ($statusResp | ConvertTo-Json -Depth 30 -Compress))
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

    $errorBody = Read-Error-Body $_

    if ($errorBody -and $errorBody.Length -gt 0) {
        Write-Log "Download error body:"
        Write-Log $errorBody
        Write-Cmd ("[ERROR]" + $errorBody)
    }
    else {
        Write-Cmd "[ERROR]$_"
    }

    exit
})";

const std::wstring SCRIPT_EXTEND_VIDEO = LR"()";

const std::wstring SCRIPT_TEXT_TO_VIDEO = LR"([Console]::OutputEncoding = [System.Text.Encoding]::UTF8
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
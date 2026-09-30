#pragma once
#include <string>

const std::wstring SCRIPT_NANO_BANANA = LR"([Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = "Stop"

$logfile = "${PARAM_IMAGE_LOG_FILE}"

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

function Get-Image-Mime-Type($filePath) {
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

function Find-Image-Block($obj) {
    if ($null -eq $obj) {
        return $null
    }

    if ($obj.PSObject.Properties.Name -contains "output_image" -and $obj.output_image -and $obj.output_image.data) {
        return $obj.output_image
    }

    if ($obj.PSObject.Properties.Name -contains "steps") {
        foreach ($step in $obj.steps) {
            if ($step.type -eq "model_output" -and $step.content) {
                foreach ($block in $step.content) {
                    if ($block.type -eq "image" -and $block.data) {
                        return $block
                    }
                }
            }
        }
    }

    return $null
}

function Find-Text-Block($obj) {
    if ($null -eq $obj) {
        return $null
    }

    if ($obj.PSObject.Properties.Name -contains "output_text" -and $obj.output_text) {
        return [string]$obj.output_text
    }

    if ($obj.PSObject.Properties.Name -contains "steps") {
        foreach ($step in $obj.steps) {
            if ($step.type -eq "model_output" -and $step.content) {
                foreach ($block in $step.content) {
                    if ($block.type -eq "text" -and $block.text) {
                        return [string]$block.text
                    }
                }
            }
        }
    }

    return $null
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -Path $ScriptDir

$apiKey                = "${PARAM_KEY}"
$prompt                = Get-Content -Path "${PARAM_PROMPT_FILE}" -Raw -Encoding UTF8
$promptText            = "Generate an actual image file from this prompt. Do not answer with text, explanation, markdown, description, or safety note. The response must contain an image output block only. Prompt: " + [string]$prompt
$output                = "${PARAM_IMAGE_NAME}"
$model                 = "${PARAM_MODEL_NAME}".Trim()
$aspectRatio           = "${PARAM_ASPECT_RATIO}".Trim()
$imageSize             = "${PARAM_IMAGE_SIZE}".Trim()
$mimeType              = "${PARAM_MIME_TYPE}".Trim()
$mode                  = "${PARAM_MODE}".Trim()
$previousInteractionId = "${PARAM_PREVIOUS_INTERACTION_ID}".Trim()
$videoUri              = "${PARAM_VIDEO_URI}".Trim()

$imagePaths = @(
    "${PARAM_IMAGE_1}",
    "${PARAM_IMAGE_2}",
    "${PARAM_IMAGE_3}",
    "${PARAM_IMAGE_4}",
    "${PARAM_IMAGE_5}",
    "${PARAM_IMAGE_6}"
)

$input = @()

if ($mode -eq "video_to_image") {
    if (-not $videoUri) {
        Write-Cmd "[ERROR]YouTube URL required"
        exit
    }

    $input += @{
        type = "video"
        uri = $videoUri
        mime_type = "video/mp4"
    }
}

$input += @{
    type = "text"
    text = $promptText
}

foreach ($imagePath in $imagePaths) {
    $imagePath = $imagePath.Trim()

    if ($imagePath.Length -eq 0) {
        continue
    }

    $input += @{
        type = "image"
        mime_type = Get-Image-Mime-Type $imagePath
        data = Read-File-Base64 $imagePath
    }
}

$bodyObject = @{
    model = $model
    input = @($input)
    response_format = @{
        type = "image"
        mime_type = $mimeType
        aspect_ratio = $aspectRatio
    }
}

if ($imageSize.Length -gt 0 -and $imageSize -ne "Default") {
    $bodyObject.response_format.image_size = $imageSize
}

if ($mode -eq "multi_turn") {
    if (-not $previousInteractionId) {
        Write-Cmd "[ERROR]Previous interaction id required. Generate an image first."
        exit
    }

    $bodyObject.previous_interaction_id = $previousInteractionId
}

if ($mode -eq "google_search") {
    $bodyObject.tools = @(@{ type = "google_search" })
}
elseif ($mode -eq "image_search") {
    $bodyObject.tools = @(@{ type = "google_search"; search_types = @("web_search", "image_search") })
}

$url = "https://generativelanguage.googleapis.com/v1beta/interactions"
$body = $bodyObject | ConvertTo-Json -Depth 50
$bytes = [System.Text.Encoding]::UTF8.GetBytes($body)

$headers = @{
    "x-goog-api-key" = $apiKey
    "Content-Type"   = "application/json"
}

Write-Log "Creating Nano Banana interaction..."
Write-Log "Mode: $mode"
Write-Log "Model: $model"
Write-Log "AspectRatio: $aspectRatio"
Write-Log "ImageSize: $imageSize"
Write-Log "MimeType: $mimeType"

try {
    $response = Invoke-RestMethod `
        -Method Post `
        -Uri $url `
        -Headers $headers `
        -Body $bytes

    Write-Log "Interaction completed"
    Write-Log ("Interaction id: " + $response.id)
    Write-Log ("Status: " + $response.status)

    $progress = @{
        id = $response.id
        status = $response.status
    }
    Write-Cmd ("[SUCCESS]" + ($progress | ConvertTo-Json -Compress))
}
catch {
    Write-Log "Failed to create Nano Banana interaction: $_"

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

$imageBlock = Find-Image-Block $response

if (-not $imageBlock) {
    $textBlock = Find-Text-Block $response
    Write-Log "No generated image block found:"
    Write-Log ($response | ConvertTo-Json -Depth 50)
    if ($textBlock) {
        Write-Log "Model returned text instead of image:"
        Write-Log $textBlock
        Write-Cmd ("[ERROR]" + (@{
            error = "Model returned text instead of image. Try rephrasing the prompt as a visual scene, or remove claims/advertising aimed at children."
            model_text = $textBlock
        } | ConvertTo-Json -Compress))
    }
    else {
        Write-Cmd ("[ERROR]" + (@{
            error = "No generated image block found in model response."
        } | ConvertTo-Json -Compress))
    }
    exit
}

try {
    $outputDir = [System.IO.Path]::GetDirectoryName($output)
    if ($outputDir) {
        [System.IO.Directory]::CreateDirectory($outputDir) | Out-Null
    }

    [System.IO.File]::WriteAllBytes($output, [System.Convert]::FromBase64String($imageBlock.data))

    $result = @{
        file = $output
        interactionId = $response.id
        mimeType = $imageBlock.mime_type
    }

    Write-Log "Image saved: $output"
    Write-Cmd ("[SUCCESS]" + ($result | ConvertTo-Json -Compress))
}
catch {
    Write-Log "Failed to save image: $_"
    Write-Cmd "[ERROR]$_"
    exit
})";

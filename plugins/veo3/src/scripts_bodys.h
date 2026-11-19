#pragma once
#include <string>

// One PowerShell script for every mode, run by CVeo3::Process with ${PARAM_...} replaced.
//
// Google shut the Veo 3.1 preview models down in the Gemini API on 22 October 2026; video now comes from
// Gemini Omni through the Interactions API (https://aistudio.google.com/docs/omni):
//   POST v1beta/interactions {model, input, response_format{type: video, aspect_ratio, resolution,
//   duration}, background: true} returns an id at once; GET v1beta/interactions/<id> until the status is
//   no longer in_progress; the video is in steps[type=model_output].content[type=video], base64 in "data"
//   (or a file "uri"). Extending a video made here continues its interaction (previous_interaction_id),
//   which Google keeps for 55 days on the paid tier - Omni has no free tier.
// Modes: text - the prompt alone; image - up to 3 pictures or short videos (references, up to 3 s) and the
// prompt, the model decides from the prompt how to use them; frames - generation_config.video_config.task image_to_video with two pictures
// (start and end, 3-10 s); extend - "Extend this video." on the earlier interaction, no task allowed with
// previous_interaction_id (3-10 s more, 30 s in all); edit - "Edit this video." on the earlier interaction
// or on an uploaded video (Files API), with up to 2 photos to put a person or object into it.
//
// Lines on stdout are what the plugin window reads: "[SUCCESS]" or "[ERROR]" and one JSON object, never the
// video data. The JSON is written by JavaScriptSerializer, which escapes quotes as \" (the window turns
// every \uXXXX into one byte before parsing, so a quote must not come as ").
//
// Values come in single-quoted strings: CVeo3::Process doubles every ' in them, and nothing in a path or the
// key is expanded. The prompt comes from script.prompt next to the script. ASCII only: this header has no BOM.
const std::wstring SCRIPT_OMNI = LR"PS([Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = 'Stop'
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor [System.Net.SecurityProtocolType]::Tls12

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -Path $ScriptDir

$logfile = Join-Path $ScriptDir '${PARAM_VIDEO_LOG_FILE}'

function Write-Log($msg) {
    $timestamp = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
    "$timestamp $msg" | Out-File -FilePath $logfile -Append -Encoding UTF8
}

function Write-Cmd($msg) {
    Write-Output (($msg -replace "`r`n", '' -replace "`n", '') + "`n")
}

Add-Type -AssemblyName System.Web.Extensions
$json = New-Object System.Web.Script.Serialization.JavaScriptSerializer
$json.MaxJsonLength = [int]::MaxValue

function Fail($message) {
    Write-Log "Error: $message"
    Write-Cmd ('[ERROR]' + $json.Serialize(@{ error = @{ message = [string]$message } }))
    exit
}

# a key of a parsed JSON object (a Dictionary: its IDictionary.Contains is not callable from PowerShell), or $null
function Get-Key($obj, $name) {
    if (($obj -is [System.Collections.IDictionary]) -and $obj.ContainsKey($name)) { return $obj[$name] }
    return $null
}

# Google's error body is {"error": {"code": ..., "message": ..., "status": ...}}
function Api-Error($r) {
    try {
        $e = Get-Key ($json.DeserializeObject($r.text)) 'error'
        $m = Get-Key $e 'message'
        if ($m) { return [string]$m }
    }
    catch { }
    return ('HTTP ' + $r.code + ' ' + $r.text)
}

# HttpWebRequest rather than Invoke-RestMethod: the error body of a 4xx is read the same way as a 200, and
# the answer with the video (megabytes of base64) is parsed without the 2 MB limit of ConvertFrom-Json
function Invoke-Api($method, $url, $bodyText) {
    $req = [System.Net.HttpWebRequest]::Create($url)
    $req.Method = $method
    $req.Headers.Add('x-goog-api-key', $apiKey)
    $req.Timeout = 120000
    $req.ReadWriteTimeout = 300000
    if ($bodyText) {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($bodyText)
        $req.ContentType = 'application/json; charset=utf-8'
        $req.ContentLength = $bytes.Length
        $stream = $req.GetRequestStream()
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Close()
    }
    return (Read-Answer $req)
}

# the status, body and upload URL of a sent request; a 4xx/5xx answer is read like a 200 one
function Read-Answer($req) {
    $resp = $null
    try { $resp = $req.GetResponse() }
    catch [System.Net.WebException] {
        $resp = $_.Exception.Response
        if ($null -eq $resp) { throw }
    }
    try {
        $reader = New-Object System.IO.StreamReader($resp.GetResponseStream(), [System.Text.Encoding]::UTF8)
        $text = $reader.ReadToEnd()
        $reader.Close()
        return @{ code = [int]$resp.StatusCode; text = $text; upload = $resp.Headers['X-Goog-Upload-URL'] }
    }
    finally { $resp.Close() }
}

# a file of the Files API is PROCESSING for a few seconds, then ACTIVE (or FAILED); returns the state
function Wait-Active($name) {
    $state = ''
    for ($i = 0; $i -lt 90; $i++) {
        try { $r = Invoke-Api 'GET' "$baseUrl/$name" $null }
        catch { $r = @{ code = 0; text = $_.Exception.Message } }
        if ($r.code -eq 200) {
            $state = [string](Get-Key ($json.DeserializeObject($r.text)) 'state')
            if ($state -ne 'PROCESSING') { break }
        }
        else { Write-Log ('File status request failed: ' + (Api-Error $r)) }
        Start-Sleep -Seconds 2
    }
    Write-Log "File $name, state $state"
    return $state
}

)PS"
LR"PS(
# Edit of a video not made here: it goes to the Files API first - a resumable upload, "start" and then the
# bytes ("upload, finalize") - and is used once ACTIVE. Google deletes it after 48 hours.
function Upload-Video($path) {
    $ext = [System.IO.Path]::GetExtension($path).ToLowerInvariant()
    $mime = 'video/mp4'
    if ($ext -eq '.mov') { $mime = 'video/quicktime' }
    elseif ($ext -eq '.webm') { $mime = 'video/webm' }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    Write-Log "Uploading $path ($($bytes.Length) bytes, $mime)"

    $req = [System.Net.HttpWebRequest]::Create('https://generativelanguage.googleapis.com/upload/v1beta/files')
    $req.Method = 'POST'
    $req.Headers.Add('x-goog-api-key', $apiKey)
    $req.Headers.Add('X-Goog-Upload-Protocol', 'resumable')
    $req.Headers.Add('X-Goog-Upload-Command', 'start')
    $req.Headers.Add('X-Goog-Upload-Header-Content-Length', [string]$bytes.Length)
    $req.Headers.Add('X-Goog-Upload-Header-Content-Type', $mime)
    $start = [System.Text.Encoding]::UTF8.GetBytes('{"file": {"display_name": "AVS video"}}')
    $req.ContentType = 'application/json'
    $req.ContentLength = $start.Length
    $stream = $req.GetRequestStream()
    $stream.Write($start, 0, $start.Length)
    $stream.Close()
    $r = Read-Answer $req
    $uploadUrl = $r.upload
    if (($r.code -ne 200) -or (-not $uploadUrl)) { Fail ('Google did not take the video: ' + (Api-Error $r)) }

    $req = [System.Net.HttpWebRequest]::Create($uploadUrl)
    $req.Method = 'POST'
    $req.Headers.Add('X-Goog-Upload-Offset', '0')
    $req.Headers.Add('X-Goog-Upload-Command', 'upload, finalize')
    $req.ContentLength = $bytes.Length
    $req.Timeout = 600000
    $req.ReadWriteTimeout = 600000
    $stream = $req.GetRequestStream()
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Close()
    $r = Read-Answer $req
    if ($r.code -ne 200) { Fail ('The video upload failed: ' + (Api-Error $r)) }
    $file = Get-Key ($json.DeserializeObject($r.text)) 'file'
    $name = [string](Get-Key $file 'name')
    $uri = [string](Get-Key $file 'uri')
    if (($name -eq '') -or ($uri -eq '')) { Fail ('The video upload failed: ' + $r.text) }
    if ((Wait-Active $name) -ne 'ACTIVE') { Fail 'Google could not read the video.' }
    return @{ uri = $uri; mime = $mime }
}

function Image-Part($path) {
    $ext = [System.IO.Path]::GetExtension($path).ToLowerInvariant()
    $mime = 'image/png'
    if (($ext -eq '.jpg') -or ($ext -eq '.jpeg')) { $mime = 'image/jpeg' }
    elseif ($ext -eq '.webp') { $mime = 'image/webp' }
    return @{ type = 'image'; mime_type = $mime; data = [System.Convert]::ToBase64String([System.IO.File]::ReadAllBytes($path)) }
}
)PS"
// MSVC takes a string literal of at most 16 KB (C2026): the script is two, joined by the compiler
LR"PS(

$apiKey      = '${PARAM_KEY}'.Trim()
$model       = '${PARAM_MODEL_NAME}'
$mode        = '${PARAM_MODE}'
$output      = '${PARAM_VIDEO_NAME}'
$resolution  = '${PARAM_RESOLUTION}'.Trim()
$aspectRatio = '${PARAM_ASPECT_RATIO}'.Trim()
$duration    = '${PARAM_SECONDS}'.Trim()
$previousId  = '${PARAM_PREVIOUS_ID}'.Trim()
$inputVideo  = '${PARAM_INPUT_VIDEO}'.Trim()
$imagePaths  = @(@('${PARAM_IMAGE_1}', '${PARAM_IMAGE_2}', '${PARAM_IMAGE_3}') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
$baseUrl     = 'https://generativelanguage.googleapis.com/v1beta'

try {
    $promptText = [string](Get-Content -Path (Join-Path $ScriptDir 'script.prompt') -Raw -Encoding UTF8)

    Write-Log "Mode: $mode, model: $model, resolution: $resolution, aspect ratio: $aspectRatio, duration: $duration s, images: $($imagePaths.Count)"

    # delivery uri: Google advises it above 4 MB (1080p, 4k, long clips); the answer then holds a file to
    # download instead of megabytes of base64 (which it may still send - both are handled below)
    $format = @{ type = 'video'; resolution = $resolution; duration = ($duration + 's'); aspect_ratio = $aspectRatio; delivery = 'uri' }
    $body = @{ model = $model; background = $true; response_format = $format }

    if ($mode -eq 'image') {
        # the model decides from the prompt how to use the pictures (characters, objects, style or a start);
        # a short video (Google: up to 3 s, up to 3 of them for 3-10 s, 1 for 11-15 s) is a reference too,
        # e.g. for a movement - uploaded to the Files API like an edited one
        $parts = @()
        foreach ($p in $imagePaths) {
            $ext = [System.IO.Path]::GetExtension($p).ToLowerInvariant()
            if (($ext -eq '.mp4') -or ($ext -eq '.mov') -or ($ext -eq '.webm')) {
                $uploaded = Upload-Video $p
                $parts += @{ type = 'video'; uri = $uploaded.uri }
            }
            else { $parts += (Image-Part $p) }
        }
        $parts += @{ type = 'text'; text = $promptText }
        $body.input = $parts
    }
    elseif ($mode -eq 'frames') {
        # task image_to_video: "the first image defines the starting frame, and the optional second image
        # defines the ending frame" - without it two pictures may be taken as references
        if ($imagePaths.Count -ne 2) { Fail 'Two pictures are needed: the first and the last frame.' }
        $body.input = @((Image-Part $imagePaths[0]), (Image-Part $imagePaths[1]), @{ type = 'text'; text = $promptText })
        $body.generation_config = @{ video_config = @{ task = 'image_to_video' } }
    }
    elseif ($mode -eq 'extend') {
        # a continuation of the earlier interaction. The API refuses a task with it ("previous_interaction_id
        # is not allowed when video task is set"), so it is asked in words, as Google's examples do -
        # otherwise the model may edit the video instead
        if ($previousId -eq '') { Fail 'No earlier video to extend.' }
        $body.previous_interaction_id = $previousId
        $body.input = ('Extend this video. ' + $promptText)
    }
    elseif ($mode -eq 'edit') {
        # a video made here goes on in its interaction, another one is uploaded first; the photos put a
        # person or an object into it ("reference images are supported for ... video editing"). Length and
        # orientation are the video's own; up to 15 s, and Google edits uploaded videos only outside the
        # EEA, Switzerland and the UK (it says so if not).
        $parts = @()
        if ($previousId -ne '') { $body.previous_interaction_id = $previousId }
        else {
            if ($inputVideo -eq '') { Fail 'No video to change.' }
            $uploaded = Upload-Video $inputVideo
            $parts += @{ type = 'video'; uri = $uploaded.uri }
        }
        foreach ($p in $imagePaths) { $parts += (Image-Part $p) }
        $parts += @{ type = 'text'; text = ('Edit this video. ' + $promptText) }
        $body.input = $parts
        $format.Remove('duration')
        $format.Remove('aspect_ratio')
    }
    else {
        $body.input = $promptText
    }

    $create = Invoke-Api 'POST' "$baseUrl/interactions" ($json.Serialize($body))
    if ($create.code -ne 200) { Fail (Api-Error $create) }

    $obj = $json.DeserializeObject($create.text)
    $id = Get-Key $obj 'id'
    $status = Get-Key $obj 'status'
    if (-not $id) { Fail ('No interaction id in the answer: ' + $create.text) }
    Write-Log "Interaction $id, status $status"

)PS"
// (a cut anywhere is fine: the pieces join into the same text, PowerShell skips the empty line)
LR"PS(
    # every line moves the progress bar of the window
    $started = Get-Date
    $failures = 0
    while ($status -eq 'in_progress') {
        Write-Cmd ('[SUCCESS]' + $json.Serialize(@{ id = $id; status = 'in_progress' }))
        if (((Get-Date) - $started).TotalMinutes -gt 30) { Fail 'The video is not ready after 30 minutes.' }
        Start-Sleep -Seconds 5
        try { $r = Invoke-Api 'GET' "$baseUrl/interactions/$id" $null }
        catch { $r = @{ code = 0; text = $_.Exception.Message } }
        if ($r.code -ne 200) {
            # a lost connection: the video goes on being made on the server, ask again for a minute
            $failures++
            Write-Log ('Status request failed: ' + (Api-Error $r))
            if ($failures -ge 12) { Fail (Api-Error $r) }
            continue
        }
        $failures = 0
        $obj = $json.DeserializeObject($r.text)
        $status = Get-Key $obj 'status'
    }
    Write-Log "Status $status after $([int]((Get-Date) - $started).TotalSeconds) s"

    # the video, or what the model said instead of making one (a refused prompt)
    $video = $null
    $said = @()
    foreach ($step in @(Get-Key $obj 'steps')) {
        if ((Get-Key $step 'type') -ne 'model_output') { continue }
        foreach ($part in @(Get-Key $step 'content')) {
            $type = Get-Key $part 'type'
            if (($type -eq 'video') -and ($null -eq $video)) { $video = $part }
            elseif ($type -eq 'text') { $said += [string](Get-Key $part 'text') }
        }
    }
    # statuses: completed, incomplete (done, results cut short), failed, cancelled, requires_action
    if ((($status -ne 'completed') -and ($status -ne 'incomplete')) -or ($null -eq $video)) {
        # a failed interaction lists its reasons in "errors" (INVALID_ARGUMENT ...)
        $message = Get-Key (Get-Key $obj 'error') 'message'
        if (-not $message) { $message = (@(Get-Key $obj 'errors') | ForEach-Object { Get-Key $_ 'message' } | Where-Object { $_ }) -join ' ' }
        if (-not $message) { $message = ($said -join ' ').Trim() }
        if (-not $message) { $message = "The video was not made (status: $status)." }
        Fail $message
    }

    $outPath = Join-Path $ScriptDir $output
    $data = Get-Key $video 'data'
    if ($data) {
        [System.IO.File]::WriteAllBytes($outPath, [System.Convert]::FromBase64String($data))
    }
    else {
        # .../v1beta/files/<id>:download?alt=media - the file is PROCESSING for a few seconds, then ACTIVE
        $uri = Get-Key $video 'uri'
        if (-not $uri) { Fail 'The answer has no video.' }
        $fileId = $uri -replace '^.*/files/', '' -replace '[:?].*$', ''
        if ((Wait-Active "files/$fileId") -eq 'FAILED') { Fail 'Google could not prepare the video file.' }
        $client = New-Object System.Net.WebClient
        $client.Headers.Add('x-goog-api-key', $apiKey)
        $client.DownloadFile($uri, $outPath)
    }
    Write-Log "Saved $outPath ($((Get-Item -LiteralPath $outPath).Length) bytes)"

    Write-Cmd ('[SUCCESS]' + $json.Serialize(@{ id = $id; status = 'completed'; file = $output }))
}
catch {
    Fail $_.Exception.Message
}
)PS";

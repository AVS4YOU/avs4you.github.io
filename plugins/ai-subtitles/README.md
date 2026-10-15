# AI Subtitles

Content plugin for AVS Video Editor, Video Converter and Audio Editor: pick a
video or audio file, and it writes `<name>.srt` next to it. Speech recognition
is [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (MIT) with the
Whisper models (MIT) and runs on the user's computer - no account, no API key,
nothing uploaded.

## What it does

1. **Model** - downloaded once into `%LOCALAPPDATA%\avs_plugin_aisubtitles\models`
   from `huggingface.co/ggerganov/whisper.cpp`, size-checked:

   | Quality | Model | Download |
   |---|---|---|
   | Fast | `ggml-base.bin` | 148 MB |
   | Balanced (default) | `ggml-small.bin` | 488 MB |
   | Best | `ggml-large-v3-turbo-q5_0.bin` | 574 MB |

2. **Audio** - `ffmpeg.exe` (the one the YouTube plugin ships) turns any video
   or audio into 16 kHz mono WAV in a scratch folder.
3. **Recognition** - whisper-cli with automatic language detection or a chosen
   language, optionally translating into English; segments are capped at two
   subtitle lines. Which build runs:
   - "Use the graphics card" ticked (x64 package): `whisper-cli-vulkan.exe`,
     on the GPU ggml picks - a discrete one, else an integrated one, else the
     CPU. If it cannot start (no `vulkan-1.dll`, i.e. no GPU driver) or fails,
     the CPU build runs instead.
   - Otherwise `whisper-cli.exe`, or `whisper-cli-compat.exe` on CPUs without AVX2.
4. **Timing** - whisper's own segment times are guesses, whole seconds with the
   smaller models, and the first ones are pinned to 0:00 when speech starts
   more than a second into the audio. So:
   - "What is in the file: Speech" runs Silero VAD (`ggml-silero-v5.1.2.bin`,
     885 KB, downloaded once from `huggingface.co/ggml-org/whisper-vad`):
     Whisper hears only the speech it finds, nothing is subtitled over music
     or silence. "Music and songs" leaves VAD out - it does not take singing
     for speech.
   - Word times come from DTW over the model's alignment heads (`-dtw`, which
     needs flash attention off), written with `-ojf`. A cue runs from its first
     word to 0.6 s after its last, no longer than reading it takes (1 s + 0.1 s
     a character), not past the end of the speech VAD found, and not into the
     next cue. With VAD on, whisper-cli maps segment times back to the original
     audio but not word times: `SrtFromJson` does, from the
     `vad_segment_info` table it logs.
5. **Tidy** - `TidySrt` trims the text, wraps each cue into at most two
   balanced lines of about 42 characters, drops empty cues, renumbers, and
   saves UTF-8 with a BOM (Windows players read anything else as ANSI).

whisper-cli reads its command line in the ANSI code page, so it only ever gets
ASCII paths: it runs in a scratch folder under the work directory and is given
relative paths. ffmpeg takes the user's file under any name.

The window remembers quality, language, content type, the translate and GPU
switches in `settings.json`. Nothing is handed back to the host: the host has no
use for an `.srt` through `AsyncCallback`, and players pick up `<name>.srt` by
its name.

## Layout

```
src/subtitles.{h,cpp}  the pipeline, no UI - models, ffmpeg, whisper-cli, cue timing, SRT tidy
src/plugin.{h,cpp}     plugin object, worker thread, settings
src/ui.{h,cpp}         the window
src/exports.cpp        the content plugin ABI
fetch.py               whisper.cpp at a pinned release -> build/<arch>/whisper-cli*.exe
```

Shared with other plugins, in `sdk/common`: `ChildProcess` (run a tool, read
its output line by line, cancel kills the process tree), `HttpDownload`
(WinHTTP, follows redirects, `.partial` until complete) and `CpuFeatures`.

## Build

From the repository root:

```bash
python build.py ai-subtitles
```

That runs `fetch.py` first, which checks out whisper.cpp v1.9.5 into
`whisper-cpp/` and builds `whisper-cli` as single static executables (`/MT`, no
OpenMP): x86 and x64, AVX2 and compat, plus `whisper-cli-vulkan.exe` for x64,
which needs the [Vulkan SDK](https://vulkan.lunarg.com) on the build machine
(found through `VULKAN_SDK` or the newest `C:\VulkanSDK\<version>`). Users need
only their GPU driver. Then the plugin DLL is built and packed with them and
`ffmpeg.exe`.

## Tested

With the pipeline driven directly (no UI) on the packed x64 build: English and
Russian speech, auto-detected; Russian translated into English; an mp3; a video
under a Cyrillic path; a video without audio (a clear error); cancelling during
the model download (no `.partial` left behind); the compat executable.

Timing, against speech found in a Demucs-separated vocals track: a 21.6-minute
dubbed sitcom episode, model small - 85% of cues start within 0.5 s of the
speech (median 0.18 s), against 39% (0.62 s) with whisper's own times. A song
in "Music and songs": 69% against 50%. Speed on an i5-11400H with an RTX 3050
Ti Laptop, 3 minutes of it: 43 s on the CPU, 11 s on the GPU, 81 s on the
Intel UHD; the first Vulkan run on a machine adds ~14 s of shader compilation.
The fallback to the CPU build was tried with a broken Vulkan executable.

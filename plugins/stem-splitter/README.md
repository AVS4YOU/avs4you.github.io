# Stem Splitter

Content plugin for AVS Audio Editor and Video Editor: pick a song or a video,
and it writes separate tracks into `<name> - stems` next to it, and adds them
to the project. Separation is Meta's [Demucs v4](https://github.com/facebookresearch/demucs)
(MIT) exported to ONNX and run by ONNX Runtime, with the STFT code of
[demucs.onnx](https://github.com/sevagh/demucs.onnx) (MIT) - on the user's
computer, no account, no API key, nothing uploaded.

| Mode | Model | Tracks |
|---|---|---|
| Vocals and instrumental (default) | `htdemucs_ft_vocals.onnx`, 174 MB | vocals; instrumental = the mix minus the vocals |
| 4 tracks | `htdemucs.onnx`, 174 MB | drums, bass, other, vocals |
| 6 tracks | `htdemucs_6s.onnx`, 115 MB | the 4 + guitar, piano |

Output is WAV 16-bit, FLAC or MP3 320 kbps; track names are in the UI language
("Song - Vocals.wav", "Song - Вокал.wav").

## How it works

1. **Model** - downloaded once into `%LOCALAPPDATA%\avs_plugin_stemsplitter\models`,
   size-checked, from the `models-demucs-onnx-1` release of this repository.
2. **Decode** - `ffmpeg.exe` turns any audio or video into raw 44.1 kHz stereo
   float in a scratch folder: `%LOCALAPPDATA%\avs_plugin_stemsplitter\tmp`, or
   an `avs-stems-<n>` subfolder of the temporary folder chosen in the window.
   It holds about 21 MB a minute per track (the input and every output) until
   the run ends.
3. **Separate** - `stems.exe` cuts the audio into jobs of 8-30 s with 0.75 s of
   context each side and runs at most 3 at once - one ONNX session, every
   logical processor split between them - and writes each track straight to
   disk, so memory does not grow with the length of the file. Every job in
   flight holds about 2.5 GB whatever its thread count, so fewer run when free
   memory (physical and commit) cannot carry 3: past it, Windows grows the page
   file on C: - 8 jobs at once took 19 GB and filled the disk. Inside a job
   Demucs runs 7.8 s segments with 25 % overlap and cross-fades them.
4. **Encode** - `ffmpeg.exe` writes each track in the chosen format.

A 4-minute song in 6 tracks takes about 110 s and 7.4 GB on an i5-11400H
(6 cores / 12 threads) - 3 jobs of 4 threads were the fastest of the splits
measured - with the same separation quality as Demucs in PyTorch. `stems.exe`
takes Unicode paths and writes its errors in UTF-8.

`stems.exe` is always 64-bit: Demucs's attention needs blocks of a few hundred
MB that a 32-bit address space cannot give. The x86 package carries the x64
engine too, and the x86 plugin runs it - which takes 64-bit Windows; on 32-bit
Windows the plugin says so. ONNX Runtime's CPU arena is off: with several runs
at once it now and then asked for one huge block and failed.

## Layout

```
src/stems.{h,cpp}        the pipeline, no UI - model, ffmpeg, stems.exe, encoding
src/plugin.{h,cpp}       plugin object, worker thread, settings (incl. the temporary folder)
src/ui.{h,cpp}           the window
src/exports.cpp          the content plugin ABI
engine/stems_cli.cpp     stems.exe: jobs, threads, raw float in and out
engine/separate.cpp      Demucs over one span (from demucs.onnx's model_apply.cpp)
fetch.py                 demucs.onnx + ONNX Runtime NuGet -> build/x64/stems.exe
```

## Build

```bash
python build.py stem-splitter
```

`fetch.py` checks out demucs.onnx at a pinned commit (with only its Eigen
submodule), unpacks ONNX Runtime 1.22.1 from NuGet and builds `engine/`.

## The models

Made from Meta's official weights by `tools/export_demucs_onnx.py`, published by
`tools/publish_assets.py` under the tag `src/stems.cpp` downloads from:

```bash
python tools/export_demucs_onnx.py models
python tools/publish_assets.py models-demucs-onnx-1 models/htdemucs.onnx models/htdemucs_6s.onnx models/htdemucs_ft_vocals.onnx
```

A changed model goes out under a new tag, with `MODEL_URL` and the sizes in
`src/stems.cpp` updated.

## Tested

With the pipeline driven directly (no UI): a video under a Cyrillic path in all
three modes and formats, x64 and the x86 plugin with the x64 engine. On a mix of
a known voice over known music the vocals come out at 12-13 dB SDR (the mix
itself: -12.7 dB), the instrumental at 20-26 dB, as with PyTorch Demucs. A
2-minute file, run repeatedly with 6 threads, for the memory behaviour. A
chosen temporary folder with a Cyrillic path and spaces: the scratch subfolder
appears there and is removed, C: is not touched.

# Voice Clone

Content plugin for AVS Audio Editor and Video Editor: give it the vocals of a
song (a voice without music - Stem Splitter takes them out of a mix) and a few
seconds of another voice, and it sings the vocals again in that voice, keeping
the melody, the rhythm and the words. The result goes next to the vocals as
`<name> - new voice.wav` (or FLAC, MP3) and into the project; putting it back
over the music is the editor's job.

Called from the context menu of an audio or video file in the editor, the
window opens with that file as the vocals (`ClickMenuItemEx`, see
`docs/ContentPlugin-README.md`); the voice sample is the one used last time.

The conversion is [Seed-VC](https://github.com/Plachtaa/seed-vc)'s singing
model (v1, F0-conditioned, 44.1 kHz) exported to ONNX and run by ONNX Runtime
with DirectML - on the graphics card of any DirectX 12 GPU, or on the CPU - on
the user's computer: no account, no API key, nothing uploaded.

## How it works

1. **Models** - downloaded once into `%LOCALAPPDATA%\avs_plugin_voiceclone\models`,
   size-checked, from the `models-seedvc-onnx-1` release of this repository:
   1.6 GB in 7 files (see [What it downloads](#what-it-downloads)).
2. **Decode** - `ffmpeg.exe` turns the vocals and the voice sample (any audio or
   video) into raw mono float at 44.1 kHz and at 16 kHz in a scratch folder
   under `%LOCALAPPDATA%\avs_plugin_voiceclone\tmp`.
3. **Convert** - `seedvc\seedvc.exe` (below) re-voices the vocals.
4. **Encode** - `ffmpeg.exe` writes the result in the chosen format.

Inside `seedvc.exe`, step by step as Seed-VC's `inference.py`:

| Step | Model | What it takes from where |
|---|---|---|
| words | Whisper-small encoder, 30 s windows, 5 s overlap | the vocals (and the sample) at 16 kHz |
| melody | RMVPE pitch, 100 frames a second | the vocals; kept in key unless asked (pitch shift, or "move into the voice's range") |
| timbre | CAMPPlus speaker embedding (Kaldi fbank) + the sample's 44.1 kHz mel | the voice sample, up to 12 s (below) |
| voice | DiT, flow matching: Euler steps with classifier-free guidance (0.7) | words + melody, conditioned on the sample |
| sound | BigVGAN v2 44 kHz vocoder | the generated mel |

The mels, the fbank, the F0 bins and the stretching to frames are numpy, checked
against the PyTorch originals (Whisper log-mel 1.6e-5, fbank 7.7e-4, mels 1e-3,
F0 bins and nearest-neighbour stretching exact).

Where it departs from `inference.py`, and why:

- **DiT window 21 s (1800 frames) with the sample instead of 30 s, sample up to
  12 s instead of 25 s.** One DirectML step must stay well under 2 s - past it
  Windows resets the graphics driver (TDR, seen as a hang) - and the cost per
  second of song is lowest with the window about twice the sample.
- **BigVGAN in 200-frame pieces with 32 frames of context each side** - for the
  same reason; the pieces join bit-exactly.
- **DiT in float16** (normalisation and softmax kept in float32): 0.65 s a step
  on an RTX 3050 Ti instead of 1.03 s, half the download; 0.4 % off float32.
- **One model on the graphics card at a time**: the feature models are freed
  before the DiT, the DiT before BigVGAN. A 4 GB card slowed down 7x with all
  of them loaded.
- **The CPU takes over** when the graphics card fails mid-run.
- **The sample starts where the voice does and ends in a pause.** How the
  sample is cut moves the whole result in time, by up to a frame. Seed-VC
  takes it as it is and its voice comes about 6 ms late against the source;
  cut in the middle of a word (as a plain 12 s limit does), or with the
  silence after the voice trimmed, 10-11 ms late - PyTorch Seed-VC the same,
  so it is the model; appending digital silence does not help. So the silence
  before the voice goes, up to 1 s after it stays as recorded, and a longer
  sample ends at the end of its last pause (100 ms or more) in the 4 s before
  the limit: on both test voices back to 6 ms, the pitch against the source
  20-21 cents at no shift, as Seed-VC's 18-20. Not understood all the way: with
  a 15 s limit the same rule came out 11 ms late again, and Seed-VC in PyTorch
  with only the silence before the voice removed and the whole tail kept
  comes in time (14 cents). At the shift that lines it up the pitch is 14-16
  cents in every case.
- **The level of the vocals it was given.** Seed-VC sings at a level of its
  own - on "Кузнец" 12 dB above the vocal stem - and BigVGAN clamps whatever
  goes past full scale. `bigvgan.onnx` is exported without that clamp, and the
  result is scaled to the integrated loudness of the source vocals (ITU-R
  BS.1770: K-weighted, gated; the same number ffmpeg's ebur128 gives), its
  peaks kept under full scale: it goes back into the mix where the old vocals
  were.
- **RMVPE in 20 s pieces** with 1.28 s of context each side: a whole song in one
  call took the graphics card past the same 2 s. Voicing differs on 0.12 % of
  the frames, the pitch by a median 0 cents.

Measured on a laptop (i5-11400H, RTX 3050 Ti 4 GB), 40 s of "Кузнец"
(Король и Шут) re-voiced with synthetic samples (edge-tts, a male and a female
voice, 16 s each): under 2 min on the GPU, the same output from `seedvc.exe`
as from the script. Against Seed-VC in PyTorch (CUDA, 30 s window, the whole
sample, 30 steps):

| | to the sample | to the singer | pitch, median | Whisper CER |
|---|---|---|---|---|
| male, here | 0.67 | 0.37 | 21 cents | 38 % |
| male, PyTorch | 0.69 | 0.36-0.40 | 18-19 cents | 39-41 % |
| female, here | 0.76 | 0.24 | 20 cents | 44 % |
| female, PyTorch | 0.81 | 0.22 | 20 cents | 39 % |

(the source: 0.10 / 0.09 to the samples). The female sample has no pause
between 9.5 and 12 s, so 9.4 s of it are used against PyTorch's 15.9 s - the
gap in timbre. Seeds change these by about 0.01 and 1 cent; float32 on the CPU
gives the same as float16 on DirectML; 30 steps no better than 25; the 30 s
window 2-3 cents better and 3.2x slower on this card.

The whole song (3:01) takes 8.3 min on the same GPU and comes out at 0.67 to
the sample, 0.39 to the singer. In 10 s pieces it is 0.59-0.69,
except in quiet passages between the verses, where the stem is 10-15 dB down
(about 0.1): Seed-VC in PyTorch does the same there.

The quality presets are 12, 25 and 40 diffusion steps. On the 40 s test the
three measure the same (0.67 to the sample, 21 cents; Whisper CER 40, 38,
33 %) in 81, 113 and 164 s - to be settled by ear.

## What it downloads

Nothing but the models, once, on the first conversion - the window says so and
how much is left (1545 MB). From
`https://github.com/AVS4YOU/avs4you.github.io/releases/download/models-seedvc-onnx-1/`
into `%LOCALAPPDATA%\avs_plugin_voiceclone\models`:

| File | Bytes | Model | Licence |
|---|---|---|---|
| `filters.npz` | 852 530 | mel filter banks | Apache-2.0 / MIT |
| `whisper_encoder.onnx` | 352 712 079 | Whisper-small encoder | Apache-2.0 |
| `campplus.onnx` | 28 690 673 | CAM++ speaker embedding | Apache-2.0 |
| `rmvpe.onnx` | 361 688 039 | RMVPE pitch | MIT |
| `length_regulator.onnx` | 33 868 204 | Seed-VC length regulator | GPL-3.0 |
| `dit_fp16.onnx` | 351 710 509 | Seed-VC diffusion transformer | GPL-3.0 |
| `bigvgan.onnx` | 490 977 308 | BigVGAN v2 vocoder | MIT |
| **total** | **1 620 499 342** | | |

A file counts as present when its size matches `src/voice.cpp`, so a later
release that changes one model downloads only that one. A file is written as
`.partial` and renamed when complete: an interrupted download starts that file
again, the finished ones are kept. `MODELS-LICENSES.txt` is published with them
and also comes in the package, under `licenses\`.

Needs 64-bit Windows 10 or 11 - the x86 package runs the 64-bit engine too -
and 1.6 GB on the system drive. A DirectX 12 graphics card is used when there
is one; the processor otherwise, several times slower (12 s of vocals at Fast:
3 min on an i5-11400H).

## Licences

- `seedvc.exe` - `engine/seedvc.py` and `engine/export_onnx.py` follow Seed-VC's
  code and are **GPL-3.0** like it. The plugin DLL runs it as a separate program
  and shares no code with it. Next to it in the package: `LICENSE` (GPL-3.0),
  `README.txt` (where its source is, as GPL-3.0 section 6 asks - this folder,
  tagged `voice-clone-v<version>` by `release.py`) and `THIRD-PARTY-NOTICES.txt`
  (Python, numpy, ONNX Runtime with DirectML, the PyInstaller bootloader, the
  Microsoft C++ runtime); `fetch.py` writes the last two from the venv it builds in.
- Models - each ONNX file is the published checkpoint of its project, converted,
  and stays under that project's licence: Seed-VC GPL-3.0, Whisper-small
  Apache-2.0, CAM++ Apache-2.0, RMVPE (RVC's weights) MIT, BigVGAN v2 MIT.
  `licenses/MODELS-LICENSES.txt` says for each where it comes from (Hugging Face
  repository and revision), what was changed, and holds the licence texts; it
  goes into the package and into the models release.
- `ffmpeg.exe` - the GPL-3.0 build the other plugins ship; `licenses/FFMPEG.txt`.
- The window asks for a voice sample the user has the right to use.

## Layout

```
src/voice.{h,cpp}        the pipeline, no UI - models, ffmpeg, seedvc.exe, encoding
src/plugin.{h,cpp}       plugin object, worker thread, settings
src/ui.{h,cpp}           the window
src/exports.cpp          the content plugin ABI
engine/seedvc.py         seedvc.exe (GPL-3.0): numpy features + ONNX Runtime
engine/export_onnx.py    Seed-VC's checkpoints -> the ONNX models, each checked against PyTorch
fetch.py                 engine/seedvc.py -> build/x64/seedvc/ with PyInstaller, and its notices
licenses/                the models' and ffmpeg's licences, packed as they are
```

`seedvc.exe` is always 64-bit; the x86 package carries it too and runs it on
64-bit Windows.

## Building

```bash
python build.py voice-clone      # fetch.py (the engine), the DLL x86+x64, the .avsp
```

`fetch.py` makes a virtual environment under `build/engine/` with pinned numpy
and onnxruntime-directml and runs PyInstaller; it replaces the C++ runtime
PyInstaller copies from Python (14.16, too old for ONNX Runtime 1.22: "DLL
initialization routine failed") with the build machine's.

## Models

Made from Seed-VC's checkpoints (which its `inference.py` downloads into its
`checkpoints` folder) in a Python with torch, transformers 4.46, librosa,
munch, einops, descript-audio-codec, onnx and onnxconverter-common:

```bash
python engine/export_onnx.py --seed-vc <Seed-VC checkout> --out models
```

then published where `src/voice.cpp` downloads them from:

```bash
python tools/publish_assets.py models-seedvc-onnx-1 models/filters.npz models/whisper_encoder.onnx models/campplus.onnx models/rmvpe.onnx models/length_regulator.onnx models/dit_fp16.onnx models/bigvgan.onnx plugins/voice-clone/licenses/MODELS-LICENSES.txt
```

`dit.onnx` (float32) is an intermediate and is not published. The sizes in
`src/voice.cpp` must match the published files.

# Stable Audio 3 AVS4YOU plugin

Native Windows content plugin backed by [sa3.cpp](https://github.com/betweentwomidnights/sa3.cpp), fetched at a pinned commit by `fetch.py`.

## Build

From the repository root, which builds and packs `build/<arch>/*.avsp`:

```powershell
python build.py StableAudio3
python build.py StableAudio3 --clean     # rebuild everything, the Vulkan backend included
```

That runs `fetch.py` here first, which owns everything third-party:

- checks out sa3.cpp (with ggml as its submodule) at the commit pinned in
  `fetch.py` into `sa3-cpp/`;
- builds its `sa3_backend` target through `CMakePresets.json`, for each
  architecture into `build/<arch>/`, one `sa3.dll` per folder:
  - `sa3-cpp-cpu` - AVX2/FMA/F16C;
  - `sa3-cpp-cpu-compat` - no AVX at all, for CPUs without AVX2;
  - `sa3-cpp-gpu` - x64 only, Vulkan.

A backend is rebuilt only when the sa3.cpp commit, `CMakeLists.txt`, its
preset in `CMakePresets.json` or the Vulkan SDK changes, or with `--clean`. The
Vulkan one needs the Vulkan SDK on the build machine (`VULKAN_SDK`, else the
newest `C:\VulkanSDK\<version>`), as AI Subtitles does; without it the GPU
backend already in `build/x64` is kept. `python fetch.py --check` reports what
is missing.

`build.py` itself then builds only `StableAudio3.dll`, with
`STABLEAUDIO3_BACKEND=NONE`, and packs it with those backends.

Everything is built with the static runtime (`/MT`) and ggml as static
libraries inside `sa3.dll`, with its own thread pool instead of OpenMP: the
package carries no Visual C++ or UCRT DLLs (1.0.x packed 4.3 MB of them per
x64 package; AVS installs the VC++ 2015-2022 Redistributable, and the UCRT is
part of Windows 10, the oldest the installers offer plugins on). ggml is built
with `GGML_NATIVE` off, for AVX2 rather than the build machine's CPU, and
without BMI2 on x86, where 32-bit MSVC lacks `_pdep_u64`.

Until 1.0.x the GPU backend was CUDA: 214 MB of kernels for six NVIDIA
generations, which also needed the CUDA 12 runtime and cuBLAS - not part of
the NVIDIA driver, so it ran only where the CUDA Toolkit was installed. Vulkan
goes through the display driver of any vendor and packs a few MB of shaders.

## Which backend runs

`src/system_info.cpp` decides at run time, so the plugin works everywhere:

- **GPU** when the x64 plugin finds, through the `vulkan-1.dll` every GPU
  driver installs in System32, a discrete or integrated GPU with Vulkan 1.2,
  the `sa3-cpp-gpu` file and an AVX2 CPU. The Device list then offers the
  graphics card; with a discrete GPU, which ggml-vulkan picks first, it
  preselects it. An integrated GPU alone is offered but not preselected. The
  probe runs once per host process, guarded: a driver that faults while queried
  only costs the GPU option.
- **CPU** otherwise: `sa3-cpp-cpu` on a CPU with AVX2, FMA, F16C and an OS that
  saves the AVX registers, else `sa3-cpp-cpu-compat`.

A GPU generation that fails - the backend does not load, video memory runs
out, the driver fails mid-run - is made again on the CPU.

Each folder's `sa3.dll` carries its own ggml, so switching between the
processor and the graphics card takes effect without restarting the host.

## Models and precision

The Model list names the three Stable Audio 3 models by what they make:
`medium` (1.5B, music and sound effects, up to 380 s, x64 only),
`small-music` and `small-sfx` (0.5B, about twice as fast, up to 120 s). The
line under it gives the download: 5.4 GB for medium, 2.1 GB for a small one,
the 1.1 GB text encoder included, which the models share.

Weights are f16, which the GGUF model cards call the production path. Settings
has "Full model precision (f32)" - twice the size, f32 medium does not fit a
4 GB card. Up to 1.1.0 the plugin picked f32 on a computer without a GPU, so a
model present only in f32 is used as it is rather than downloaded again.

The backends are loaded from the plugin folder. Models downloaded by the GUI
(the public GGUF sets on Hugging Face, over WinHTTP) and generated WAV files go
to `%LOCALAPPDATA%\avs_plugin_stableaudio3`.

Generated WAV files are placed in `%LOCALAPPDATA%\avs_plugin_stableaudio3` and returned to the host through `AsyncCallback`, matching the Sora2 plugin contract.



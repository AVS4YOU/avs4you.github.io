# Stable Audio 3 AVS4YOU plugin

Native Windows content plugin backed by [sa3.cpp](https://github.com/betweentwomidnights/sa3.cpp), fetched at a pinned commit by `fetch.py`.

## Build

From the repository root, which builds and packs `build/<arch>/*.avsp`:

```powershell
python build.py StableAudio3
python build.py StableAudio3 --clean     # rebuild everything, the CUDA backend included
```

That runs `fetch.py` here first, which owns everything third-party:

- checks out sa3.cpp (with ggml as its submodule) at the commit pinned in
  `fetch.py` into `sa3-cpp/`;
- builds its `sa3_backend` target through `CMakePresets.json`, for each
  architecture into `build/<arch>/`:
  - `sa3-cpp-cpu` - AVX2/FMA/F16C;
  - `sa3-cpp-cpu-compat` - no AVX at all, for CPUs without AVX2;
  - `sa3-cpp-gpu` - x64 only, CUDA.

A backend is rebuilt only when the sa3.cpp commit, `CMakeLists.txt`, its
preset in `CMakePresets.json` or the CUDA version changes, or with `--clean`. The CUDA one
needs the CUDA Toolkit 12.8 or newer with its Visual Studio 2019 integration
(older toolkits cannot compress the kernels and make `ggml-cuda.dll` about four
times larger) and takes the better part of an hour. Without a toolkit the GPU
backend already in `build/x64` is kept. `python fetch.py --check` reports what is
missing.

`build.py` itself then builds only `StableAudio3.dll`, with
`STABLEAUDIO3_BACKEND=NONE`, and packs it with those backends.

ggml is built with `GGML_NATIVE` off, for AVX2 rather than the build machine's
CPU, and without BMI2 on x86, where 32-bit MSVC lacks `_pdep_u64`.

## Which backend runs

`src/system_info.cpp` decides at run time, so the plugin works everywhere:

- **GPU** when the x64 plugin finds an NVIDIA driver with a CUDA device, the
  `sa3-cpp-gpu` files, a CUDA 12 runtime (`cudart64_12`, `cublas64_12`,
  `cublasLt64_12` - bundled in `sa3-cpp-gpu` or on the PATH) and an AVX2 CPU.
  The Device list then offers `gpu` and preselects it; otherwise it only offers
  `cpu`. A GPU backend that still fails to load falls back to the CPU.
- **CPU** otherwise: `sa3-cpp-cpu` on a CPU with AVX2, FMA, F16C and an OS that
  saves the AVX registers, else `sa3-cpp-cpu-compat`.

Windows reuses an already loaded `ggml.dll` by name, so within one host process
the first backend loaded wins: switching from `cpu` to `gpu` after a CPU
generation keeps running on the CPU until the host restarts.

On first use the plugin copies the packaged CPU/CUDA backend DLLs into `%LOCALAPPDATA%\\avs_plugin_stableaudio3`. Models downloaded by the GUI and generated WAV files are stored in that same working directory. Package `sa3-cpp-cpu/` and `sa3-cpp-gpu/` beside `StableAudio3.dll`. CUDA requires a compatible NVIDIA driver/toolkit. The GUI downloads the selected public GGUF set from Hugging Face using the Windows-provided `curl.exe`.

Generated WAV files are placed in `%LOCALAPPDATA%\avs_plugin_stableaudio3` and returned to the host through `AsyncCallback`, matching the Sora2 plugin contract.



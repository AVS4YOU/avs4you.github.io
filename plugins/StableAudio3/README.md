# Stable Audio 3 AVS4YOU plugin

Native Windows content plugin backed by [sa3.cpp](https://github.com/betweentwomidnights/sa3.cpp) as a git submodule.

## Build

```powershell
git submodule update --init --recursive
cmake --preset windows-cpu
cmake --build --preset cpu-release
cmake --preset windows-cuda
cmake --build --preset cuda-release
```

On first use the plugin copies the packaged CPU/CUDA backend DLLs into `%LOCALAPPDATA%\\avs_plugin_stableaudio3`. Models downloaded by the GUI and generated WAV files are stored in that same working directory. Package `sa3-cpp-cpu/` and `sa3-cpp-gpu/` beside `StableAudio3.dll`. CUDA requires a compatible NVIDIA driver/toolkit. The GUI downloads the selected public GGUF set from Hugging Face using the Windows-provided `curl.exe`.

Generated WAV files are placed in `%LOCALAPPDATA%\avs_plugin_stableaudio3` and returned to the host through `AsyncCallback`, matching the Sora2 plugin contract.



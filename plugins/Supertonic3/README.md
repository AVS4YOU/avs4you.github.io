# Supertonic 3 plugin for AVS Audio Editor

Native VS2019 content plugin that generates speech locally with Supertonic 3 and returns the WAV file to AVS Audio Editor through the SDK callback.

## Runtime directory

The plugin always uses:

```text
%LOCALAPPDATA%\avs_plugin_supertonic3\
├── models\
│   ├── onnx\
│   └── voice_styles\
└── supertonic3-<timestamp>.wav
```

On first use, the packaged `supertonic-3` model directory is copied into the local `models` directory. `SetTemporaryPath` does not override this location.

## Build

1. Restore `packages.config` into the project-local `packages` directory.
2. Open `Supertonic3.vcxproj` in Visual Studio 2019.
3. Build `Release | x64`.
4. Package `Supertonic3.dll`, `onnxruntime.dll`, and the `supertonic-3` directory together.
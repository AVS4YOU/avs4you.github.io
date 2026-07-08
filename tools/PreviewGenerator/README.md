# PreviewGenerator

PreviewGenerator is a Windows command-line tool that generates GIF previews for AVS4YOU effect plugins.
It loads a plugin DLL, reads an input PNG, calls the plugin `ApplyEffect` export, and writes an animated GIF.

## Features

- Standard animated preview using plugin completeness values from `0.0` to `1.0`.
- `-side-by-side` before/after comparison slider GIF.
- Automatic side-by-side mode for plugins with `GetEffectsCount() > 1`.
- Effect names are read with `GetEffectName` when the export is available.
- `-ping-pong` reverse-loop animation.
- `-duration <seconds>` to control one forward pass duration.
- `-force-completeness <value>` to pass a fixed `dCompleteness` into `ApplyEffect` while keeping frame timing unchanged.
- Plugin temp state is passed through `void** effectData` and released with `ReleaseEffectData` when available.

## Command line

```powershell
PreviewGenerator.exe plugin.dll input.png output.gif [fps] [options]
```

Options:

```text
-side-by-side
-ping-pong
-duration <seconds>
-force-completeness <value>
```

Examples:

```powershell
PreviewGenerator.exe effect.dll input.png output.gif 25
PreviewGenerator.exe effect.dll input.png output.gif 25 -duration 2
PreviewGenerator.exe effect.dll input.png output.gif 25 -force-completeness 1
PreviewGenerator.exe effect.dll input.png comparison.gif -side-by-side 25 -duration 2
PreviewGenerator.exe effect.dll input.png comparison.gif -side-by-side -ping-pong 25 -duration 2
```

## Project layout

```text
PreviewGenerator.cpp
third_party/gif/gif.h
third_party/stb/stb_image.h
```

Visual Studio project files are intentionally not stored in the repository. Generate them with CMake.

## Generate and build with CMake

Most AVS4YOU plugins used by this tool are 32-bit, so use the `Win32` generator platform unless your plugin DLL is x64.

Win32:

```powershell
cmake -S . -B build-win32 -G "Visual Studio 16 2019" -A Win32
cmake --build build-win32 --config Debug
```

x64:

```powershell
cmake -S . -B build-x64 -G "Visual Studio 16 2019" -A x64
cmake --build build-x64 --config Debug
```

Generated executables:

```text
build-win32/Debug/PreviewGenerator.exe
build-x64/Debug/PreviewGenerator.exe
```

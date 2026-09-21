# Effect Plugin API - Developer Guide

## Overview

Effect Plugin is a plugin type that provides various effects for image or video processing. These plugins can apply filters, transformations, and other visual effects to media content in AVS4YOU applications.

## Plugin Type

When implementing the `PluginType()` function, return:
```cpp
Plugins::PluginType::ImageEffect
```

## How to Create an Effect Plugin

### 1. Project Structure

Create a dynamic-link library (DLL) project in Visual Studio or another IDE.

**Required files:**
- `CEffectPluginIntf.h` - effect plugin interface
- `CBase.h` - base definitions
- `AVSConsts.h` - AVS application constants

**Additional dependencies:**
- `oleauto.h` - for BSTR string operations

### 2. Required Exported Functions

Your plugin must export the following functions:

#### Information functions:
- `PluginType()` - returns plugin type
- `PluginId()` - unique plugin identifier
- `PluginName()` - plugin name
- `PluginVersion()` - plugin version
- `PluginIcon()` - plugin icon path
- `IsApplicationSupported(int appId)` - checks application support
- `PluginInfo()` - additional information (JSON)

#### Effect operations:
- `GetEffectsCount()` - number of effects in the plugin
- `GetEffectName(int index)` - effect name
- `GetEffectId(int index)` - effect identifier
- `GetEffectParams(int index)` - effect parameters
- `ApplyEffect(...)` - applies effect to image

#### Memory management:
- `ReleasePluginString(wchar_t* str)` - releases strings
- `ReleaseEffectData(void* data)` - releases effect data

### 3. Minimal Plugin Example

```cpp
#define PLUGIN_EXPORTS
#include "CEffectPluginIntf.h"
#include <string>

// Information functions
PLUGIN_API Plugins::PluginType __stdcall PluginType() {
    return Plugins::PluginType::ImageEffect;
}

PLUGIN_API wchar_t* __stdcall PluginId() {
    return _wcsdup(L"MyEffectPlugin.plugin");
}

PLUGIN_API wchar_t* __stdcall PluginName() {
    return _wcsdup(L"My Effect Plugin");
}

PLUGIN_API wchar_t* __stdcall PluginVersion() {
    return _wcsdup(L"1.0.0");
}

PLUGIN_API wchar_t* __stdcall PluginIcon() {
    return _wcsdup(L"C:\\path\\to\\icon.png");
}

PLUGIN_API bool __stdcall IsApplicationSupported(int appId) {
    // Support for AVS Video Editor and Photo Editor
    return appId == AVS_VIDEO_EDITOR || appId == AVS_PHOTO_EDITOR;
}

PLUGIN_API wchar_t* __stdcall PluginInfo() {
    return _wcsdup(L"{\"author\":\"My Company\",\"description\":\"Cool effects\"}");
}

// Effect operations
PLUGIN_API int __stdcall GetEffectsCount() {
    return 2; // We have 2 effects
}

PLUGIN_API wchar_t* __stdcall GetEffectName(int index) {
    switch(index) {
        case 0: return _wcsdup(L"Blur Effect");
        case 1: return _wcsdup(L"Sharpen Effect");
        default: return _wcsdup(L"Unknown");
    }
}

PLUGIN_API int __stdcall GetEffectId(int index) {
    return index + 1; // IDs start from 1
}

PLUGIN_API wchar_t* __stdcall GetEffectParams(int index) {
    switch(index) {
        case 0:
            return _wcsdup(L"{\"radius\":{\"type\":\"int\",\"min\":1,\"max\":100,\"default\":5}}");
        case 1:
            return _wcsdup(L"{\"strength\":{\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"default\":0.5}}");
        default:
            return _wcsdup(L"{}");
    }
}

// Apply effect
HRESULT PLUGIN_API __stdcall ApplyEffect(
    BYTE* data,
    int width,
    int height,
    double timestamp,
    int paramCount,
    const BSTR* params,
    void** effectData
) {
    // Implement effect logic here
    // data - pixel array in BGRA format (4 bytes per pixel)
    // Process image...

    return S_OK;
}

// Memory release
PLUGIN_API void __stdcall ReleasePluginString(wchar_t* str) {
    free(str);
}

PLUGIN_API void __stdcall ReleaseEffectData(void* data) {
    free(data);
}
```

## API Reference

### Information Functions

#### `PluginType()`
```cpp
PLUGIN_API Plugins::PluginType __stdcall PluginType()
```
**Description:** Returns the plugin type.

**Returns:** `Plugins::PluginType::ImageEffect`

---

#### `PluginId()`
```cpp
PLUGIN_API wchar_t* __stdcall PluginId()
```
**Description:** Returns the unique plugin identifier. It is also the name of the folder the plugin is installed into (`%APPDATA%\AVS4YOU\Plugins\<id>`, `Plugins-x64` for 64-bit hosts), so keep it a plain folder name - ASCII letters, digits, `.`, `_` and `-`, ending in `.plugin` - conventionally `<Name>.plugin`. It must be identical to `pluginId` in the plugin's `config.json`: installers read that field from [plugins.json](PluginsManifest.md) to tell whether the plugin is already installed. `new_effect.py` writes both from one value, and `check_effect.py` compares them.

**Returns:** String containing the plugin ID (e.g., "EffectVHS.plugin").

**Note:** Memory must be released using `ReleasePluginString()`.

**Example:**
```cpp
return _wcsdup(L"EffectCool.plugin");
```

---

#### `PluginName()`
```cpp
PLUGIN_API wchar_t* __stdcall PluginName()
```
**Description:** Returns the plugin name.

**Returns:** String containing the plugin name.

**Note:** Memory must be released using `ReleasePluginString()`.

---

#### `PluginVersion()`
```cpp
PLUGIN_API wchar_t* __stdcall PluginVersion()
```
**Description:** Returns the plugin version.

**Returns:** String containing the version (e.g., "1.0.0").

**Note:** Memory must be released using `ReleasePluginString()`.

---

#### `PluginIcon()`
```cpp
PLUGIN_API wchar_t* __stdcall PluginIcon()
```
**Description:** Returns the path to the plugin icon.

**Returns:** String containing the icon file path.

**Note:** Memory must be released using `ReleasePluginString()`.

**Difference from Content Plugin:** Does not take a handle parameter, as the icon is common for all effects.

---

#### `IsApplicationSupported(int appId)`
```cpp
PLUGIN_API bool __stdcall IsApplicationSupported(int appId)
```
**Description:** Checks if the specified application is supported by the plugin.

**Parameters:**
- `appId` - application identifier (constants from AVSConsts.h)

**Returns:** `true` if the application is supported, otherwise `false`.

**Recommended applications for effects:**
- `AVS_VIDEO_EDITOR` (4) - AVS Video Editor
- `AVS_PHOTO_EDITOR` (5) - AVS Photo Editor
- `AVS_IMAGE_CONVERTER` (51) - AVS Image Converter

---

#### `PluginInfo()`
```cpp
PLUGIN_API wchar_t* __stdcall PluginInfo()
```
**Description:** Returns additional information about the plugin in JSON format.

**Returns:** JSON string with additional information.

**Note:** Memory must be released using `ReleasePluginString()`.

**Difference from Content Plugin:** Does not take a handle parameter.

**Example JSON:**
```json
{
  "author": "My Company",
  "description": "Professional image effects",
  "website": "https://example.com",
  "category": "Filters"
}
```

---

### Memory Management

#### `ReleasePluginString(wchar_t* str)`
```cpp
PLUGIN_API void __stdcall ReleasePluginString(wchar_t* str)
```
**Description:** Releases memory allocated for a plugin string.

**Parameters:**
- `str` - pointer to the string to be released

**Important:** Must be called for ALL strings obtained from the plugin.

---

#### `ReleaseEffectData(void* data)`
```cpp
PLUGIN_API void __stdcall ReleaseEffectData(void* data)
```
**Description:** Releases memory allocated for effect data.

**Parameters:**
- `data` - pointer to the data to be released

**Purpose:** Used to release data returned through the `effectData` parameter of the `ApplyEffect()` function.

---

### Effect Operations

#### `GetEffectsCount()`
```cpp
PLUGIN_API int __stdcall GetEffectsCount()
```
**Description:** Returns the number of available effects in the plugin.

**Returns:** Integer - number of effects.

**Note:** Plugin can contain one or multiple effects.

---

#### `GetEffectName(int index)`
```cpp
PLUGIN_API wchar_t* __stdcall GetEffectName(int index)
```
**Description:** Returns the effect name by index.

**Parameters:**
- `index` - effect index (from 0 to GetEffectsCount()-1)

**Returns:** String containing the effect name.

**Note:** Memory must be released using `ReleasePluginString()`.

**Example:**
```cpp
switch(index) {
    case 0: return _wcsdup(L"Gaussian Blur");
    case 1: return _wcsdup(L"Motion Blur");
    case 2: return _wcsdup(L"Edge Detection");
}
```

---

#### `GetEffectId(int index)`
```cpp
PLUGIN_API int __stdcall GetEffectId(int index)
```
**Description:** Returns the unique effect identifier by index.

**Parameters:**
- `index` - effect index (from 0 to GetEffectsCount()-1)

**Returns:** Integer - effect identifier.

**Purpose:** Used for unambiguous effect identification.

---

#### `GetEffectParams(int index)`
```cpp
PLUGIN_API wchar_t* __stdcall GetEffectParams(int index)
```
**Description:** Returns effect parameters by index.

**Parameters:**
- `index` - effect index (from 0 to GetEffectsCount()-1)

**Returns:** JSON string describing effect parameters.

**Note:** Memory must be released using `ReleasePluginString()`.

**JSON parameter format:**
```json
{
  "radius": {
    "type": "int",
    "min": 1,
    "max": 100,
    "default": 5,
    "description": "Blur radius in pixels"
  },
  "intensity": {
    "type": "float",
    "min": 0.0,
    "max": 1.0,
    "default": 0.5,
    "description": "Effect intensity"
  },
  "color": {
    "type": "color",
    "default": "#FF0000",
    "description": "Effect color"
  }
}
```

**Parameter types:**
- `int` - integer number
- `float` - floating-point number
- `bool` - boolean value
- `string` - string
- `color` - color (in #RRGGBB format)
- `enum` - enumeration

---

#### `ApplyEffect(BYTE* data, int width, int height, double timestamp, int paramCount, const BSTR* params, void** effectData)`
```cpp
HRESULT PLUGIN_API __stdcall ApplyEffect(
    BYTE* data,
    int width,
    int height,
    double timestamp,
    int paramCount,
    const BSTR* params,
    void** effectData
)
```
**Description:** Applies an effect to image data.

**Parameters:**
- `data` - pointer to the image pixel array (BGRA format, 4 bytes per pixel)
- `width` - image width in pixels
- `height` - image height in pixels
- `timestamp` - timestamp (for video), in seconds
- `paramCount` - number of effect parameters
- `params` - array of effect parameters (BSTR strings)
- `effectData` - [output] pointer for returning additional effect data

**Returns:**
- `S_OK` (0) - effect successfully applied
- HRESULT error code - on error

**Image data format:**
```
Byte array: [B, G, R, A, B, G, R, A, ...]
Array size: width * height * 4 bytes
Pixel order: left to right, top to bottom
```

**Pixel access:**
```cpp
int pixelIndex = (y * width + x) * 4;
BYTE blue  = data[pixelIndex + 0];
BYTE green = data[pixelIndex + 1];
BYTE red   = data[pixelIndex + 2];
BYTE alpha = data[pixelIndex + 3];
```

**Effect application example:**
```cpp
HRESULT PLUGIN_API __stdcall ApplyEffect(
    BYTE* data,
    int width,
    int height,
    double timestamp,
    int paramCount,
    const BSTR* params,
    void** effectData
) {
    // Validate input data
    if (!data || width <= 0 || height <= 0) {
        return E_INVALIDARG;
    }

    // Get parameters
    float intensity = 0.5f;
    if (paramCount > 0 && params[0]) {
        intensity = (float)_wtof(params[0]);
    }

    // Apply effect to each pixel
    int totalPixels = width * height;
    for (int i = 0; i < totalPixels; i++) {
        int offset = i * 4;

        // Example: color inversion with intensity
        data[offset + 0] = (BYTE)(data[offset + 0] * (1 - intensity) + (255 - data[offset + 0]) * intensity);
        data[offset + 1] = (BYTE)(data[offset + 1] * (1 - intensity) + (255 - data[offset + 1]) * intensity);
        data[offset + 2] = (BYTE)(data[offset + 2] * (1 - intensity) + (255 - data[offset + 2]) * intensity);
        // Alpha channel usually unchanged
    }

    // Optional: return additional data
    *effectData = nullptr;

    return S_OK;
}
```

**Important notes:**
1. Modify data directly in the `data` array
2. Do not allocate new memory for the image (unless size change is required)
3. Handle all errors and return appropriate HRESULT codes
4. For video, use `timestamp` for time-dependent effects
5. The `effectData` parameter is used to pass state between frames

---

## Effect Examples

### 1. Simple Box Blur Effect

```cpp
HRESULT ApplyBoxBlur(BYTE* data, int width, int height, int radius) {
    if (radius < 1) return S_OK;

    // Create temporary buffer
    BYTE* temp = new BYTE[width * height * 4];
    memcpy(temp, data, width * height * 4);

    // Apply blur
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sumB = 0, sumG = 0, sumR = 0;
            int count = 0;

            // Iterate over pixel neighborhood
            for (int dy = -radius; dy <= radius; dy++) {
                for (int dx = -radius; dx <= radius; dx++) {
                    int nx = x + dx;
                    int ny = y + dy;

                    if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                        int offset = (ny * width + nx) * 4;
                        sumB += temp[offset + 0];
                        sumG += temp[offset + 1];
                        sumR += temp[offset + 2];
                        count++;
                    }
                }
            }

            // Write average value
            int offset = (y * width + x) * 4;
            data[offset + 0] = sumB / count;
            data[offset + 1] = sumG / count;
            data[offset + 2] = sumR / count;
        }
    }

    delete[] temp;
    return S_OK;
}
```

### 2. Brightness Effect

```cpp
HRESULT ApplyBrightness(BYTE* data, int width, int height, float brightness) {
    // brightness: -1.0 (darker) to 1.0 (lighter)
    int adjustment = (int)(brightness * 255);

    int totalPixels = width * height;
    for (int i = 0; i < totalPixels; i++) {
        int offset = i * 4;

        for (int c = 0; c < 3; c++) { // B, G, R channels
            int value = data[offset + c] + adjustment;
            data[offset + c] = (BYTE)max(0, min(255, value));
        }
    }

    return S_OK;
}
```

### 3. Grayscale Effect

```cpp
HRESULT ApplyGrayscale(BYTE* data, int width, int height) {
    int totalPixels = width * height;

    for (int i = 0; i < totalPixels; i++) {
        int offset = i * 4;

        BYTE b = data[offset + 0];
        BYTE g = data[offset + 1];
        BYTE r = data[offset + 2];

        // Grayscale conversion formula
        BYTE gray = (BYTE)(0.299 * r + 0.587 * g + 0.114 * b);

        data[offset + 0] = gray;
        data[offset + 1] = gray;
        data[offset + 2] = gray;
    }

    return S_OK;
}
```

---

## Development Guidelines

### 1. Performance
- Effect should run fast, as it may be applied to each video frame
- Use optimized algorithms
- Consider using SIMD instructions (SSE, AVX) for acceleration
- Cache data between calls when possible

### 2. Memory Management
- Always release allocated memory
- Use the `effectData` parameter for data that needs to be returned
- Check memory allocation success

### 3. Error Handling
- Validate input parameters
- Return correct HRESULT codes
- Do not throw exceptions across DLL boundary
- Log errors for debugging

### 4. Effect Parameters
- Provide reasonable default values
- Specify minimum and maximum values
- Add parameter descriptions
- Use correct data types

### 5. Testing
- Test with different image sizes
- Check edge cases (very small/large parameter values)
- Test performance on large images
- Verify correctness on video (if supported)

---

## Differences from Content Plugin

| Aspect | Content Plugin | Effect Plugin |
|--------|----------------|---------------|
| Purpose | Provides UI and menus | Image/video processing |
| Instances | Requires CreatePlugin/DeletePlugin | Not required |
| Icon | PluginIcon(handle) | PluginIcon() |
| Main function | ClickMenuItem() | ApplyEffect() |
| Data handling | Does not process pixels | Processes pixel array |
| State | Can store state in handle | Stateless (or via effectData) |

---

## JSON Structure for Effect Parameters

### Complete Example
```json
{
  "radius": {
    "type": "int",
    "min": 1,
    "max": 100,
    "default": 5,
    "step": 1,
    "description": "Blur radius in pixels",
    "label": "Radius"
  },
  "intensity": {
    "type": "float",
    "min": 0.0,
    "max": 1.0,
    "default": 0.5,
    "step": 0.1,
    "description": "Effect intensity",
    "label": "Intensity"
  },
  "mode": {
    "type": "enum",
    "values": ["fast", "normal", "quality"],
    "default": "normal",
    "description": "Quality mode",
    "label": "Mode"
  },
  "enabled": {
    "type": "bool",
    "default": true,
    "description": "Enable effect",
    "label": "Enabled"
  }
}
```

---

## SDK Package

All required header files are located in the `sdk/include/` directory:
- `CEffectPluginIntf.h` - Effect plugin interface
- `CBase.h` - base definitions and types
- `AVSConsts.h` - AVS application constants
- `CPluginBase.h` - base plugin class (optional)

---

## Additional Resources

- Plugin examples: `examples/effect-plugin/`
- Content Plugin documentation: `docs/ContentPlugin-README.md`
- GitHub: https://github.com/avs4you/marketplace
- Image processing algorithms: https://en.wikipedia.org/wiki/Digital_image_processing

---

*Document version: 1.0*
*Last updated: 2026-01-23*

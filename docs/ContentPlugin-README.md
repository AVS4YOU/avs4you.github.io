# Content Plugin API - Developer Guide

## Overview

Content Plugin is a plugin type that provides menu items, UI elements, and context-specific functionality. These plugins can add custom menu items to AVS4YOU application context menus, create custom dialogs, and interact with users.

## Plugin Type

When implementing the `PluginType()` function, return:
```cpp
Plugins::PluginType::Content
```

## How to Create a Content Plugin

### 1. Project Structure

Create a dynamic-link library (DLL) project in Visual Studio or another IDE.

**Required files:**
- `CContentPluginIntf.h` - plugin interface
- `CBase.h` - base definitions
- `AVSConsts.h` - AVS application constants

### 2. Required Exported Functions

Your plugin must export the following functions:

#### Information functions (without instance creation):
- `PluginType()` - returns plugin type
- `PluginId()` - unique plugin identifier
- `PluginName()` - plugin name
- `PluginVersion()` - plugin version
- `IsApplicationSupported(int appId)` - checks application support

#### Lifecycle management:
- `CreatePlugin()` - creates plugin instance
- `DeletePlugin(PluginHandle)` - deletes plugin instance

#### Instance operations:
- `PluginIcon(PluginHandle)` - icon path
- `PluginInfo(PluginHandle)` - additional information (JSON)
- Configuration and menu functions

### 3. Minimal Plugin Example

```cpp
#define PLUGIN_EXPORTS
#include "CContentPluginIntf.h"
#include <string>

// Plugin data
class MyPlugin {
public:
    std::wstring language = L"en";
    void* parentWindow = nullptr;
    std::wstring tempPath;
};

// Information functions
PLUGIN_API Plugins::PluginType __stdcall PluginType() {
    return Plugins::PluginType::Content;
}

PLUGIN_API wchar_t* __stdcall PluginId() {
    return _wcsdup(L"MyContentPlugin.plugin");
}

PLUGIN_API wchar_t* __stdcall PluginName() {
    return _wcsdup(L"My Content Plugin");
}

PLUGIN_API wchar_t* __stdcall PluginVersion() {
    return _wcsdup(L"1.0.0");
}

PLUGIN_API bool __stdcall IsApplicationSupported(int appId) {
    // Support for AVS Video Editor
    return appId == AVS_VIDEO_EDITOR;
}

// Instance management
PLUGIN_API PluginHandle __stdcall CreatePlugin() {
    return new MyPlugin();
}

PLUGIN_API void __stdcall DeletePlugin(PluginHandle handle) {
    delete static_cast<MyPlugin*>(handle);
}

// String release
PLUGIN_API void __stdcall ReleasePluginString(wchar_t* str) {
    free(str);
}
```

## API Reference

### Plugin Lifecycle Management

#### `CreatePlugin()`
```cpp
PLUGIN_API PluginHandle __stdcall CreatePlugin()
```
**Description:** Creates a new plugin instance.

**Returns:** Handle (pointer) to the created plugin instance.

**Note:** Memory must be released using `DeletePlugin()`.

---

#### `DeletePlugin(PluginHandle handle)`
```cpp
PLUGIN_API void __stdcall DeletePlugin(PluginHandle handle)
```
**Description:** Deletes a plugin instance and releases its resources.

**Parameters:**
- `handle` - handle to the plugin instance to be deleted

---

### Plugin Information

#### `PluginType()`
```cpp
PLUGIN_API Plugins::PluginType __stdcall PluginType()
```
**Description:** Returns the plugin type.

**Returns:** `Plugins::PluginType::Content`

---

#### `PluginId()`
```cpp
PLUGIN_API wchar_t* __stdcall PluginId()
```
**Description:** Returns the unique plugin identifier. It is also the name of the folder the plugin is installed into (`%APPDATA%\AVS4YOU\Plugins\<id>`, `Plugins-x64` for 64-bit hosts), so keep it a plain folder name - ASCII letters, digits, `.`, `_` and `-`, ending in `.plugin` - conventionally `<Name>.plugin`. It must be identical to `pluginId` in the plugin's `config.json`: installers read that field from [plugins.json](PluginsManifest.md) to tell whether the plugin is already installed.

**Returns:** String containing the plugin ID (e.g., "Sora2.plugin").

**Note:** Memory must be released using `ReleasePluginString()`.

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

#### `PluginIcon(PluginHandle handle)`
```cpp
PLUGIN_API wchar_t* __stdcall PluginIcon(PluginHandle handle)
```
**Description:** Returns the path to the plugin icon.

**Parameters:**
- `handle` - handle to the plugin instance

**Returns:** String containing the icon file path.

**Note:** Memory must be released using `ReleasePluginString()`.

---

#### `IsApplicationSupported(int appId)`
```cpp
PLUGIN_API bool __stdcall IsApplicationSupported(int appId)
```
**Description:** Checks if the specified application is supported by the plugin.

**Parameters:**
- `appId` - application identifier (constants from AVSConsts.h)

**Returns:** `true` if the application is supported, otherwise `false`.

**Application constants:**
- `AVS_VIDEO_EDITOR` (4) - AVS Video Editor
- `AVS_AUDIO_EDITOR` (1) - AVS Audio Editor
- `AVS_PHOTO_EDITOR` (5) - AVS Photo Editor
- And others from AVSConsts.h

---

#### `PluginInfo(PluginHandle handle)`
```cpp
PLUGIN_API wchar_t* __stdcall PluginInfo(PluginHandle handle)
```
**Description:** Returns additional information about the plugin in JSON format.

**Parameters:**
- `handle` - handle to the plugin instance

**Returns:** JSON string with additional information.

**Note:** Memory must be released using `ReleasePluginString()`.

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

### Plugin Configuration

#### `SetLanguage(PluginHandle handle, const wchar_t* language)`
```cpp
PLUGIN_API void __stdcall SetLanguage(PluginHandle handle, const wchar_t* language)
```
**Description:** Sets the language for the plugin interface.

**Parameters:**
- `handle` - handle to the plugin instance
- `language` - language code (e.g., "en", "ru", "de")

**Purpose:** Affects localization of menu items and UI elements.

---

#### `SetParentWindow(PluginHandle handle, void* hwnd)`
```cpp
PLUGIN_API void __stdcall SetParentWindow(PluginHandle handle, void* hwnd)
```
**Description:** Sets the parent window for plugin dialogs.

**Parameters:**
- `handle` - handle to the plugin instance
- `hwnd` - window handle (HWND cast to void*)

**Purpose:** Used for proper modal dialog behavior and ownership.

---

#### `SetTemporaryPath(PluginHandle handle, wchar_t* path)`
```cpp
PLUGIN_API void __stdcall SetTemporaryPath(PluginHandle handle, wchar_t* path)
```
**Description:** Sets the temporary files path for the plugin.

**Parameters:**
- `handle` - handle to the plugin instance
- `path` - path to the temporary directory

**Purpose:** Plugin should use this directory for any temporary files.

---

#### `CleanTemporaryFiles(PluginHandle handle)`
```cpp
PLUGIN_API void __stdcall CleanTemporaryFiles(PluginHandle handle)
```
**Description:** Cleans up temporary files created by the plugin.

**Parameters:**
- `handle` - handle to the plugin instance

**When to call:** Should be called when plugin is no longer needed.

---

#### `CleanCachedData(PluginHandle handle)`
```cpp
PLUGIN_API void __stdcall CleanCachedData(PluginHandle handle)
```
**Description:** Cleans up cached data created by the plugin.

**Parameters:**
- `handle` - handle to the plugin instance

**When to call:** Should be called before plugin is uninstalled.

---

### Menu and UI Operations

#### `GetMenuForContext(PluginHandle handle, Plugins::ContextType context)`
```cpp
PLUGIN_API wchar_t* __stdcall GetMenuForContext(PluginHandle handle, Plugins::ContextType context)
```
**Description:** Gets the menu structure for a specific context.

**Parameters:**
- `handle` - handle to the plugin instance
- `context` - context type from `Plugins::ContextType` enumeration

**Returns:** XML or JSON string describing the context menu structure.

**Note:** Memory must be released using `ReleasePluginString()`.

**Context types:**
- `ContextType::Unknown` (0) - Unknown context
- `ContextType::MediaLibrary` (1) - Media library
- `ContextType::Video` (2) - Video context
- `ContextType::Audio` (3) - Audio context
- `ContextType::Text` (4) - Text context
- `ContextType::Image` (5) - Image context

---

#### `GetPluginMenu(PluginHandle handle)`
```cpp
PLUGIN_API wchar_t* __stdcall GetPluginMenu(PluginHandle handle)
```
**Description:** Gets the main plugin menu structure.

**Parameters:**
- `handle` - handle to the plugin instance

**Returns:** XML or JSON string describing the plugin menu structure.

**Note:** Memory must be released using `ReleasePluginString()`.

---

#### `GetIconById(PluginHandle handle, int iconId)`
```cpp
PLUGIN_API wchar_t* __stdcall GetIconById(PluginHandle handle, int iconId)
```
**Description:** Gets an icon by its identifier.

**Parameters:**
- `handle` - handle to the plugin instance
- `iconId` - icon identifier

**Returns:** String containing icon path or data.

**Note:** Memory must be released using `ReleasePluginString()`.

---

#### `ClickMenuItem(PluginHandle handle, int itemId)`
```cpp
PLUGIN_API void __stdcall ClickMenuItem(PluginHandle handle, int itemId)
```
**Description:** Handles a menu item click event.

**Parameters:**
- `handle` - handle to the plugin instance
- `itemId` - menu item identifier

**Purpose:** Called when user clicks a menu item provided by the plugin.

---

### Callback Management

#### `SetCallbackHandler(PluginHandle handle, AsyncCallback callback, void* userData)`
```cpp
PLUGIN_API void __stdcall SetCallbackHandler(PluginHandle handle, AsyncCallback callback, void* userData)
```
**Description:** Sets the callback handler for asynchronous operations.

**Parameters:**
- `handle` - handle to the plugin instance
- `callback` - callback function pointer
- `userData` - user-defined data to be passed to the callback

**AsyncCallback type:**
```cpp
typedef void (*AsyncCallback)(wchar_t*, wchar_t*, int, void*);
```

**Purpose:** Used for notifications and asynchronous operation results.

---

## Development Guidelines

### 1. Memory Management
- All strings returned from the plugin must be heap-allocated (use `_wcsdup()` or `malloc()`).
- The application will release strings through your `ReleasePluginString()` function.
- Ensure that `ReleasePluginString()` correctly frees memory.

### 2. Thread Safety
- Plugin may be called from different threads.
- Use synchronization when accessing shared data.

### 3. Error Handling
- Avoid throwing exceptions across DLL boundary.
- Handle all errors inside the plugin.

### 4. Testing
- Test plugin with all supported applications.
- Verify correct memory release.
- Test language switching.

---

## Usage Examples

### Menu Structure Example (JSON)
```json
{
  "items": [
    {
      "id": 1,
      "title": "My Plugin Action",
      "icon": 100
    },
    {
      "id": 2,
      "title": "Settings",
      "icon": 101
    }
  ]
}
```

### Menu Click Handler Example
```cpp
PLUGIN_API void __stdcall ClickMenuItem(PluginHandle handle, int itemId) {
    MyPlugin* plugin = static_cast<MyPlugin*>(handle);

    switch(itemId) {
        case 1:
            // Perform action
            DoAction(plugin);
            break;
        case 2:
            // Open settings
            ShowSettings(plugin);
            break;
    }
}
```

---

## SDK Package

All required header files are located in the `sdk/include/` directory:
- `CContentPluginIntf.h` - Content plugin interface
- `CBase.h` - base definitions and types
- `AVSConsts.h` - AVS application constants
- `CPluginBase.h` - base plugin class (optional)

---

## Additional Resources

- Plugin examples: `examples/content-plugin/`
- Effect Plugin documentation: `docs/EffectPlugin-README.md`
- GitHub: https://github.com/avs4you/marketplace

---

*Document version: 1.0*
*Last updated: 2026-01-23*

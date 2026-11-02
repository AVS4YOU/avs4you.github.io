//---------------------------------------------------------------------------
/**
 * @file CContentPluginIntf.h
 * @brief Content plugin interface
 * @details Defines the API for content plugins that provide menu items,
 *          UI elements, and context-specific functionality
 */

#ifndef CContentPluginIntfH
#define CContentPluginIntfH
//---------------------------------------------------------------------------

#include "CBase.h"

#ifdef __cplusplus
extern "C" {
#endif

    // ========================================================================
    // Plugin Lifecycle Management
    // ========================================================================

    /**
     * @brief Creates a new plugin instance
     * @return Handle to the created plugin instance
     * @note Must be released using DeletePlugin() when no longer needed
     */
    PLUGIN_API PluginHandle __stdcall CreatePlugin();

    /**
     * @brief Function pointer type for CreatePlugin
     */
    typedef PluginHandle (__stdcall *LPCREATEPLUGIN)();

    /**
     * @brief Deletes a plugin instance and releases its resources
     * @param handle Handle to the plugin instance to be deleted
     */
    PLUGIN_API void __stdcall DeletePlugin(PluginHandle handle);

    /**
     * @brief Function pointer type for DeletePlugin
     */
    typedef void (__stdcall *LPDELETEPLUGIN)(PluginHandle);

    // ========================================================================
    // Plugin Information
    // ========================================================================

    /**
     * @brief Returns the plugin type
     * @return Plugin type from the Plugins::PluginType enumeration
     */
    PLUGIN_API Plugins::PluginType __stdcall PluginType();

    /**
     * @brief Function pointer type for PluginType
     */
    typedef Plugins::PluginType (__stdcall *LPPLUGINTYPE)();

    /**
     * @brief Returns the unique plugin identifier
     * @return Pointer to a string containing the plugin ID (wide characters)
     * @note Memory must be released using ReleasePluginString()
     */
    PLUGIN_API wchar_t* __stdcall PluginId();

    /**
     * @brief Function pointer type for PluginId
     */
    typedef wchar_t* (__stdcall *LPPLUGINID)();

    /**
     * @brief Returns the plugin name
     * @return Pointer to a string containing the plugin name (wide characters)
     * @note Memory must be released using ReleasePluginString()
     */
    PLUGIN_API wchar_t* __stdcall PluginName();

    /**
     * @brief Function pointer type for PluginName
     */
    typedef wchar_t* (__stdcall *LPPLUGINNAME)();

    /**
     * @brief Returns the plugin version
     * @return Pointer to a string containing the plugin version (wide characters)
     * @note Memory must be released using ReleasePluginString()
     */
    PLUGIN_API wchar_t* __stdcall PluginVersion();

    /**
     * @brief Function pointer type for PluginVersion
     */
    typedef wchar_t* (__stdcall *LPPLUGINVERSION)();

    /**
     * @brief Returns the path to the plugin icon
     * @param handle Handle to the plugin instance
     * @return Pointer to a string containing the icon path (wide characters)
     * @note Memory must be released using ReleasePluginString()
     */
    PLUGIN_API wchar_t* __stdcall PluginIcon(PluginHandle handle);

    /**
     * @brief Function pointer type for PluginIcon
     */
    typedef wchar_t* (__stdcall *LPPLUGINICON)(PluginHandle);

    /**
     * @brief Checks if an application is supported
     * @param appId Application identifier
     * @return true if the application is supported, false otherwise
     */
    PLUGIN_API bool __stdcall IsApplicationSupported(int appId);

    /**
     * @brief Function pointer type for IsApplicationSupported
     */
    typedef bool (__stdcall *LPISAPPLICATIONSUPPORTED)(int);

    /**
     * @brief Returns extra info about plugin
     * @return json
     */
    PLUGIN_API wchar_t* __stdcall PluginInfo(PluginHandle);

    /**
     * @brief Function pointer type for PluginInfo
     */
    typedef wchar_t* (__stdcall *LPPLUGININFO)(PluginHandle);

    // ========================================================================
    // Memory Management
    // ========================================================================

    /**
     * @brief Releases memory allocated for a plugin string
     * @param str Pointer to the string to be released
     * @details Must be called for all strings obtained from the plugin
     */
    PLUGIN_API void __stdcall ReleasePluginString(wchar_t* str);

    /**
     * @brief Function pointer type for ReleasePluginString
     */
    typedef void (__stdcall *LPRELEASEPLUGINSSTRING)(wchar_t*);

    // ========================================================================
    // Plugin Configuration
    // ========================================================================

    /**
     * @brief Sets the language for the plugin interface
     * @param handle Handle to the plugin instance
     * @param language Language code (e.g., "en", "ru", "de")
     * @details Affects localization of menu items and UI elements
     */
    PLUGIN_API void __stdcall SetLanguage(PluginHandle handle, const wchar_t* language);

    /**
     * @brief Function pointer type for SetLanguage
     */
    typedef void (__stdcall *LPSETLANGUAGE)(PluginHandle, const wchar_t*);

    /**
     * @brief Sets the parent window for plugin dialogs
     * @param handle Handle to the plugin instance
     * @param hwnd Handle to the parent window (HWND cast to void*)
     * @details Used for proper modal dialog behavior and ownership
     */
    PLUGIN_API void __stdcall SetParentWindow(PluginHandle handle, void* hwnd);

    /**
     * @brief Function pointer type for SetParentWindow
     */
    typedef void (__stdcall *LPSETPARENTWINDOW)(PluginHandle, void*);

    /**
     * @brief Sets the temporary files path for the plugin
     * @param handle Handle to the plugin instance
     * @param path Path to the temporary directory
     * @details Plugin should use this directory for any temporary files
     */
    PLUGIN_API void __stdcall SetTemporaryPath(PluginHandle handle, wchar_t* path);

    /**
     * @brief Function pointer type for SetTemporaryPath
     */
    typedef void (__stdcall *LPSETTEMPORARYPATH)(PluginHandle, wchar_t*);

    /**
     * @brief Cleans up temporary files created by the plugin
     * @param handle Handle to the plugin instance
     * @details Should be called when plugin is no longer needed
     */
    PLUGIN_API void __stdcall CleanTemporaryFiles(PluginHandle handle);

    /**
     * @brief Function pointer type for CleanTemporaryFiles
     */
    typedef void (__stdcall *LPCLEANTEMPORARYFILES)(PluginHandle);

    /**
     * @brief Cleans up cached data created by the plugin
     * @param handle Handle to the plugin instance
     * @details Should be called before plugin will be uninstalled
     */
    PLUGIN_API void __stdcall CleanCachedData(PluginHandle handle);

    /**
     * @brief Function pointer type for CleanCachedData
     */
    typedef void (__stdcall *LPCLEANCACHEDDATA)(PluginHandle);

    // ========================================================================
    // Menu and UI Operations
    // ========================================================================

    /**
     * @brief Gets the menu structure for a specific context
     * @param handle Handle to the plugin instance
     * @param context Context type from Plugins::ContextType enumeration
     * @return Pointer to a string containing menu definition (wide characters)
     * @note Memory must be released using ReleasePluginString()
     * @details Returns XML or JSON string describing the context menu structure
     */
    PLUGIN_API wchar_t* __stdcall GetMenuForContext(PluginHandle handle, Plugins::ContextType context);

    /**
     * @brief Function pointer type for GetMenuForContext
     */
    typedef wchar_t* (__stdcall *LPGETMENUFORCONTEXT)(PluginHandle, Plugins::ContextType);

    /**
     * @brief Gets the main plugin menu structure
     * @param handle Handle to the plugin instance
     * @return Pointer to a string containing menu definition (wide characters)
     * @note Memory must be released using ReleasePluginString()
     * @details Returns XML or JSON string describing the plugin menu structure
     */
    PLUGIN_API wchar_t* __stdcall GetPluginMenu(PluginHandle handle);

    /**
     * @brief Function pointer type for GetPluginMenu
     */
    typedef wchar_t* (__stdcall *LPGETPLUGINMENU)(PluginHandle);

    /**
     * @brief Gets an icon by its identifier
     * @param handle Handle to the plugin instance
     * @param iconId Icon identifier
     * @return Pointer to a string containing icon path or data (wide characters)
     * @note Memory must be released using ReleasePluginString()
     */
    PLUGIN_API wchar_t* __stdcall GetIconById(PluginHandle handle, int iconId);

    /**
     * @brief Function pointer type for GetIconById
     */
    typedef wchar_t* (__stdcall *LPGETICONBYID)(PluginHandle, int);

    /**
     * @brief Handles a menu item click event
     * @param handle Handle to the plugin instance
     * @param itemId Menu item identifier
     * @details Called when user clicks a menu item provided by the plugin
     */
    PLUGIN_API void __stdcall ClickMenuItem(PluginHandle handle, int itemId);

    /**
     * @brief Function pointer type for ClickMenuItem
     */
    typedef void (__stdcall *LPCLICKMENUITEM)(PluginHandle, int);

    /**
     * @brief Handles a menu item click event with data from the host app (optional export)
     * @param handle Handle to the plugin instance
     * @param itemId Menu item identifier
     * @param params JSON string with host data, e.g.
     *        {"version":1,"context":2,"files":["D:\\Video\\clip.mp4"]}
     *        where "context" is Plugins::ContextType value of the menu and
     *        "files" are the media files selected in the host app
     * @details Called instead of ClickMenuItem when the host has data for the plugin
     *          (e.g. media file of the context menu). The string is valid only during the call,
     *          the plugin must copy what it needs. Unknown JSON fields must be ignored.
     */
    PLUGIN_API void __stdcall ClickMenuItemEx(PluginHandle handle, int itemId, const wchar_t* params);

    /**
     * @brief Function pointer type for ClickMenuItemEx
     */
    typedef void (__stdcall *LPCLICKMENUITEMEX)(PluginHandle, int, const wchar_t*);

    // ========================================================================
    // Callback Management
    // ========================================================================

    /**
     * @brief Sets the callback handler for asynchronous operations
     * @param handle Handle to the plugin instance
     * @param callback Callback function pointer
     * @param userData User-defined data to be passed to the callback
     * @details Used for notifications and asynchronous operation results
     */
    PLUGIN_API void __stdcall SetCallbackHandler(PluginHandle handle, AsyncCallback callback, void* userData);

    /**
     * @brief Function pointer type for SetCallbackHandler
     */
    typedef void (__stdcall *LPSETCALLBACKHANDLER)(PluginHandle, AsyncCallback, void*);

#ifdef __cplusplus
}
#endif

#endif
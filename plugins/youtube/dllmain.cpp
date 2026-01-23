// dllmain.cpp : Defines the entry point for the DLL application.
#define PLUGIN_EXPORTS
#include <Windows.h>
#include "ShlObj.h"

#include "../../sdk/include/CContentPluginIntf.h"
#include "../../sdk/include/AVSConsts.h"
#include "../../sdk/translate/translate.h"
#include "../../sdk/common/utils.h"

#include "./src/yt_downloader.h"
#include "resource.h"

#include <iostream>

#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

HMODULE g_hInst = NULL;

WCHAR* export_str(const WCHAR* ptr)
{
    if (ptr == NULL)
        return NULL;

    size_t len = wcslen(ptr);
    WCHAR* result = new WCHAR[len + 1];

    if (result == NULL)
        return NULL;

    wcscpy_s(result, len + 1, ptr);

    return result;
}
void release_export_ptr(const WCHAR* ptr)
{
    delete[] ptr;
}

wchar_t* TR(const wchar_t* name)
{
    std::wstring item = CTranslate::GetInstance().GetManager()->Translate(name);
    return export_str(item.c_str());
}

class CYoutubePlugin : public NSProcesses::CProcessRunnerCallback
{
public:
    std::wstring m_workDirectory;
    std::wstring m_workDirectoryTemp;

    std::wstring m_path = L"";
    std::wstring m_progress = L"";

    std::wstring m_moduleDirectory;

    HWND m_hWnd = NULL;
    
    AsyncCallback m_callback = nullptr;
    void* m_callback_context = nullptr;

    ytdl::YouTubeDownloader* m_downloader = nullptr;

public:
    CYoutubePlugin()
    {
        wchar_t sAppDataLocal[65535];

        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, sAppDataLocal)))
        {
            m_workDirectory = std::wstring(sAppDataLocal);
        }
        else
        {
            m_workDirectory = NSSystemUtils::GetTempDirectory();
        }

        if (true)
        {
            wchar_t dllPath[MAX_PATH];
            GetModuleFileNameW(g_hInst, dllPath, MAX_PATH);
            std::wstring dir(dllPath);
            size_t pos = dir.find_last_of(L"\\/");
            m_moduleDirectory = dir.substr(0, pos);
        }

        m_workDirectory += L"\\avs_plugin_youtube";
        CreateDirectoryW(m_workDirectory.c_str(), NULL);
        m_workDirectoryTemp = m_workDirectory + L"\\temp";

        std::wstring iconPath = m_workDirectory + L"\\icon_internal.ico";
        std::wstring iconPathApp = m_workDirectory + L"\\icon.ico";
        if (!NSSystemUtils::ExistsFile(iconPath))
        {
            HMODULE hModule = g_hInst;
            HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(IDR_ICON), RT_RCDATA);
            if (hResource)
            {
                DWORD size = SizeofResource(hModule, hResource);
                HGLOBAL hLoaded = LoadResource(hModule, hResource);
                if (hLoaded)
                {
                    void* pLocked = LockResource(hLoaded);
                    if (pLocked)
                    {
                        HANDLE hFile = CreateFileW(
                            iconPath.c_str(),
                            GENERIC_WRITE,
                            0,
                            NULL,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL
                        );

                        if (hFile != INVALID_HANDLE_VALUE)
                        {
                            DWORD bytesWritten = 0;
                            BOOL result = WriteFile(
                                hFile,
                                pLocked,
                                static_cast<DWORD>(size),
                                &bytesWritten,
                                NULL
                            );

                            CloseHandle(hFile);
                        }
                    }
                }
            }
        }

        ::CopyFileW(iconPath.c_str(), iconPathApp.c_str(), FALSE);
    }
    virtual ~CYoutubePlugin()
    {
    }
    virtual Plugins::PluginType Type() const
    {
        return Plugins::PluginType::Content;
    }

    virtual void process_callback(const int& id, const NSProcesses::StreamType& type, const std::string& message)
    {
        std::wstring caption = L"";
        switch (type)
        {
        case NSProcesses::StreamType::StdOut:
            if (message.find("[download]") != std::string::npos) 
            {
                caption = (L"Youtube: " + NSStringUtils::utf8_to_wstring(message));
            }
            break;
        case NSProcesses::StreamType::StdErr:
            caption = (L"Youtube: [ERROR] " + NSStringUtils::utf8_to_wstring(message));
            break;
        case NSProcesses::StreamType::Stop:
        {
            std::wstring folder = m_workDirectoryTemp + L"\\*.mp4";

            WIN32_FIND_DATAW findData;
            HANDLE hFind = FindFirstFileW(folder.c_str(), &findData);

            m_path = L"";

            if (hFind != INVALID_HANDLE_VALUE)
            {
                std::wstring file = m_workDirectoryTemp + L"\\" + findData.cFileName;
                FindClose(hFind);
                m_path = m_workDirectory + L"\\" + findData.cFileName;

                ::MoveFileW(file.c_str(), m_path.c_str());
            }

            if (!m_path.empty() && NSSystemUtils::ExistsFile(m_path))
            {
                caption = (L"Youtube: [READY!]");
            }
            else
            {
                caption = (L"Youtube: [ERROR]");
            }

            break;
        }
        default:
            break;
        }

        if (!caption.empty())
            SendMessageW(m_hWnd, WM_SETTEXT, 0, (LPARAM)caption.c_str());
    }

    void Stop()
    {
        if (m_downloader)
            delete m_downloader;
        m_downloader = nullptr;
    }

    void Start(const std::wstring& url)
    {
        Stop();

        ::RemoveDirectoryW(m_workDirectoryTemp.c_str());
        ::CreateDirectoryW(m_workDirectoryTemp.c_str(), NULL);

        m_downloader = new ytdl::YouTubeDownloader(m_moduleDirectory, this);
        
        ytdl::DownloadOptions options;
        options.quality = ytdl::Quality::P360;
        options.format = ytdl::Format::MP4;
        options.outputPath = m_workDirectoryTemp;
        options.outputTemplate = L"%(title)s.%(ext)s";

        m_downloader->download(url, options);
    }
};

namespace NSUI
{
    // UI
    void CenterWindow(HWND hwnd)
    {
        RECT rcWin;
        GetWindowRect(hwnd, &rcWin);

        RECT rcWork;
        if (true)
        {
            HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfo(hMonitor, &mi))
            {
                rcWork = mi.rcWork;
            }
            else
            {
                SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);
            }
        }

        int winWidth = rcWin.right - rcWin.left;
        int winHeight = rcWin.bottom - rcWin.top;

        int x = rcWork.left + (rcWork.right - rcWork.left - winWidth) / 2;
        int y = rcWork.top + (rcWork.bottom - rcWork.top - winHeight) / 2;

        SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
    }

    LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        static HWND hEdit;
        HWND hBtnOK, hBtnCancel, hDownload;

        if (msg == WM_NCCREATE)
        {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        CYoutubePlugin* plugin = (CYoutubePlugin*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

        switch (msg)
        {
        case WM_CREATE:
        {
            hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                10, 10, 370, 25, hwnd, NULL, GetModuleHandle(NULL), NULL);

            hDownload = CreateWindow(L"BUTTON", L"Download",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                390, 10, 100, 25, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);

            hBtnOK = CreateWindow(L"BUTTON", L"Ok",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                180, 55, 60, 25, hwnd, (HMENU)2, GetModuleHandle(NULL), NULL);

            hBtnCancel = CreateWindow(L"BUTTON", L"Cancel",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                260, 55, 60, 25, hwnd, (HMENU)3, GetModuleHandle(NULL), NULL);

            HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            SendMessage(hEdit, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(hDownload, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(hBtnOK, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(hBtnCancel, WM_SETFONT, (WPARAM)font, TRUE);

            plugin->m_hWnd = hwnd;
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case 1: // Download
            {
                int len = GetWindowTextLength(hEdit);
                if (len > 0)
                {
                    std::wstring url(len, L'\0');
                    GetWindowText(hEdit, &url[0], len + 1);
                    
                    plugin->Start(url);
                }
                break;
            }
            case 2: // OK
            {
                plugin->Stop();

                if (!plugin->m_path.empty() && NSSystemUtils::ExistsFile(plugin->m_path))
                {
                    if (plugin->m_callback)
                    {
                        plugin->m_callback(PluginId(), export_str(plugin->m_path.c_str()), 0, plugin->m_callback_context);
                    }
                }

                DestroyWindow(hwnd);
                break;
            }
            case 3: // Cancel
            {
                plugin->Stop();
                DestroyWindow(hwnd);
                break;
            }
            default:
                break;
            }
            break;
        case WM_DESTROY:
            plugin->Stop();
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
        return 0;
    }

    void ShowPromptWindow(CYoutubePlugin* plugin)
    {
        ACTCTX actCtx = { sizeof(ACTCTX) };
        actCtx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
        actCtx.lpResourceName = MAKEINTRESOURCE(1);
        actCtx.hModule = g_hInst;

        HANDLE hActCtx = CreateActCtx(&actCtx);
        ULONG_PTR cookie = 0;

        if (hActCtx != INVALID_HANDLE_VALUE)
            ActivateActCtx(hActCtx, &cookie);
        
        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icex);

        WNDCLASSEX wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = MainWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"YoutubeMainWindowClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

        std::wstring iconPath = plugin->m_workDirectory + L"\\icon_internal.ico";

        HMODULE hModule = GetModuleHandle(NULL);
        wc.hIcon = (HICON)LoadImage(hModule, iconPath.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
        wc.hIconSm = (HICON)LoadImage(hModule, iconPath.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE | LR_DEFAULTCOLOR);

        RegisterClassEx(&wc);

        DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        std::wstring titleWindow = CTranslate::GetInstance().GetManager()->Translate(L"Youtube");

        RECT rc = { 0, 0, 500, 90 };
        AdjustWindowRectEx(&rc, dwStyle, FALSE, 0);

        HWND hwnd = CreateWindowEx(0, wc.lpszClassName, titleWindow.c_str(),
            dwStyle,
            CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
            NULL, NULL, GetModuleHandle(NULL), plugin);

        CenterWindow(hwnd);
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (hActCtx != INVALID_HANDLE_VALUE) {
            DeactivateActCtx(0, cookie);
            ReleaseActCtx(hActCtx);
        }
    }
}

extern "C" {

    PLUGIN_API PluginHandle __stdcall CreatePlugin()
    {
        return new CYoutubePlugin();
    }
    PLUGIN_API void __stdcall DeletePlugin(PluginHandle p)
    {
        if (p)
            delete p;
    }

    PLUGIN_API Plugins::PluginType __stdcall PluginType()
    {
        return Plugins::PluginType::Content;
    }

    PLUGIN_API wchar_t* __stdcall PluginId()
    {
        return export_str(L"Youtube.plugin");
    }

    PLUGIN_API wchar_t* __stdcall PluginName()
    {
        return TR(L"Youtube");
    }

    PLUGIN_API wchar_t* __stdcall PluginVersion()
    {
        return export_str(L"1.0.0");
    }

    PLUGIN_API wchar_t* __stdcall PluginIcon(PluginHandle plugin)
    {
        CYoutubePlugin* pluginYoutube = (CYoutubePlugin*)plugin;
        std::wstring iconPath = pluginYoutube->m_workDirectory + L"\\icon.ico";
        return export_str(iconPath.c_str());
    }

    PLUGIN_API bool __stdcall IsApplicationSupported(int id)
    {
        switch (id)
        {
        case AVS_VIDEO_CONVERTER:
        case AVS_VIDEO_EDITOR:
            return true;
        default:break;
        }

        return false;
    }

    PLUGIN_API void __stdcall ReleasePluginString(wchar_t* ptr)
    {
        release_export_ptr(ptr);
    }

    PLUGIN_API void __stdcall SetLanguage(PluginHandle, const wchar_t* name)
    {
        CTranslate::GetInstance().GetManager()->SetLang(NSStringUtils::wstring_to_utf8(name));
    }

    PLUGIN_API wchar_t* __stdcall GetMenuForContext(PluginHandle, Plugins::ContextType type)
    {
        nlohmann::json json = {};
        if (Plugins::ContextType::MediaLibrary == type) 
        {
            nlohmann::json obj;
            obj["text"] = NSStringUtils::wstring_to_utf8(CTranslate::GetInstance().GetManager()->Translate(L"Youtube"));
            obj["icon"] = 0;
            obj["action"] = 0;

            json.push_back(obj);
        }

        std::string value = json.dump();
        std::wstring valueW = NSStringUtils::utf8_to_wstring(value);

        return export_str(valueW.c_str());
    }

    PLUGIN_API wchar_t* __stdcall GetPluginMenu(PluginHandle handle)
    {
        return GetMenuForContext(handle, Plugins::ContextType::MediaLibrary);
    }

    PLUGIN_API wchar_t* __stdcall GetIconById(PluginHandle plugin, int id)
    {
        CYoutubePlugin* pluginYoutube = (CYoutubePlugin*)plugin;
        std::wstring iconPath = pluginYoutube->m_workDirectory + L"\\icon.ico";
        return export_str(iconPath.c_str());
    }
    
    PLUGIN_API void __stdcall SetCallbackHandler(PluginHandle plugin, AsyncCallback callback, void* context)
    {
        CYoutubePlugin* pluginYoutube = (CYoutubePlugin*)plugin;
        pluginYoutube->m_callback = callback;
        pluginYoutube->m_callback_context = context;
    }

    PLUGIN_API void __stdcall CleanTemporaryFiles(PluginHandle plugin)
    {
    }
    
    PLUGIN_API void __stdcall ClickMenuItem(PluginHandle handle, int id)
    {
        NSUI::ShowPromptWindow((CYoutubePlugin*)handle);
    }
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        g_hInst = hModule;
        CTranslate::GetInstance().Init(g_hInst, IDR_TRANSLATION);
        break;
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}


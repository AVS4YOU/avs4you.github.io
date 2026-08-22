// dllmain.cpp : Defines the entry point for the DLL application.
#define PLUGIN_EXPORTS
#include <Windows.h>
#include "ShlObj.h"

#include "../../sdk/include/CContentPluginIntf.h"
#include "../../sdk/include/AVSConsts.h"
#include "../../sdk/translate/translate.h"
#include "../../sdk/common/utils.h"

#include "./src/yt_downloader.h"
#include "./src/failure_reason.h"
#include "./src/output_name.h"
#include "resource.h"

#include <cctype>
#include <cstring>
#include <vector>

#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

HMODULE g_hInst = NULL;

// Both are posted from the yt-dlp reader thread so that starting the next
// attempt, and showing the final message box, happen on the UI thread. Doing
// either from inside the process callback would drive the process runner - and a
// modal dialog owned by another thread's window - from the wrong thread.
#define WM_YT_NEXT_ATTEMPT (WM_APP + 1)
#define WM_YT_FAILED       (WM_APP + 2)

// The sign-in dialog polls the browser instead of waiting on it from a worker
// thread: no second thread means no chance of it outliving the window.
#define YT_LOGIN_TIMER_ID   1
#define YT_LOGIN_POLL_MS    500

// Version of the yt-dlp binary in resources/, shown in the About box. Update
// this together with the executable.
#define YT_DLP_BUNDLED_VERSION L"2026.08.19"

class CYoutubePlugin;

namespace NSUI
{
    // Defined after CYoutubePlugin, called from inside it.
    bool ShowLoginDialog(HWND parent, const std::wstring& workDirectory, const std::wstring& host);
    void ShowAboutDialog(HWND parent);
}

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

/** Translated string for in-process use. */
std::wstring tr(const wchar_t* name)
{
    return CTranslate::GetInstance().GetManager()->Translate(name);
}

/** Translated string with %1 substituted, for in-process use. */
std::wstring tr(const wchar_t* name, const std::wstring& argument)
{
    std::wstring text = tr(name);
    NSStringUtils::replace(text, L"%1", argument);
    return text;
}

/** Translated string handed to the host, which frees it via ReleasePluginString. */
wchar_t* TR(const wchar_t* name)
{
    return export_str(tr(name).c_str());
}

class CYoutubePlugin : public NSProcesses::CProcessRunnerCallback
{
public:
    std::wstring m_workDirectory;
    std::wstring m_workDirectoryTemp;

    std::wstring m_path = L"";

    std::wstring m_moduleDirectory;

    HWND m_hWnd = NULL;

    AsyncCallback m_callback = nullptr;
    void* m_callback_context = nullptr;

    ytdl::YouTubeDownloader* m_downloader = nullptr;

    // Cookie sources still to try for the current URL, and where we are in
    // that list. See src/browser_cookies.h for how the list is built.
    std::wstring                       m_url;
    std::vector<ytdl::CookieStrategy>  m_strategies;
    size_t                             m_attempt = 0;
    std::string                        m_log;
    std::string                        m_lastError;
    bool                               m_done = false;

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

    void SetCaption(const std::wstring& caption)
    {
        if (m_hWnd && !caption.empty())
            SendMessageW(m_hWnd, WM_SETTEXT, 0, (LPARAM)caption.c_str());
    }

    /**
     * Percentage out of a yt-dlp progress line, or -1 when there is none.
     * The lines look like "[download]  45.2% of 342.21KiB at 1.42MiB/s ETA 00:03".
     */
    static int ProgressPercent(const std::string& line)
    {
        const size_t mark = line.find('%');
        if (mark == std::string::npos)
            return -1;

        size_t start = mark;
        while (start > 0 && (isdigit((unsigned char)line[start - 1]) || line[start - 1] == '.'))
            --start;

        if (start == mark)
            return -1;

        return (int)atof(line.substr(start, mark - start).c_str());
    }

    virtual void process_callback(const int& id, const NSProcesses::StreamType& type, const std::string& message)
    {
        switch (type)
        {
        case NSProcesses::StreamType::StdOut:
        {
            AppendLog(message);

            // Only the percentage reaches the title. Which cookie source is
            // being tried is an implementation detail, not news for the user.
            if (message.find("[download]") != std::string::npos)
            {
                const int percent = ProgressPercent(message);
                if (percent >= 0)
                    SetCaption(tr(L"Downloading...") + L" " + std::to_wstring(percent) + L"%");
            }
            break;
        }
        case NSProcesses::StreamType::StdErr:
            AppendLog(message);
            break;

        case NSProcesses::StreamType::Stop:
        {
            m_path = FindDownloadedFile();

            if (!m_path.empty() && NSSystemUtils::ExistsFile(m_path))
            {
                m_done = true;
                SetCaption(tr(L"Complete"));
            }
            else if (m_attempt + 1 < m_strategies.size() && NSYoutube::WorthAnotherCookieSource(m_log))
            {
                // No file this time. Try the next cookie source - but do it on
                // the UI thread, because we are inside the process runner's own
                // callback right now. The title stays as it is: walking the
                // sources is not something the user needs to watch.
                if (m_hWnd)
                    PostMessageW(m_hWnd, WM_YT_NEXT_ATTEMPT, 0, 0);
            }
            else
            {
                m_done = true;
                SetCaption(tr(L"Error"));

                if (m_hWnd)
                    PostMessageW(m_hWnd, WM_YT_FAILED, 0, 0);
            }

            break;
        }
        default:
            break;
        }
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

        m_url = url;
        m_strategies = ytdl::BuildCookieStrategies(m_workDirectory);
        m_attempt = 0;
        m_done = false;

        m_downloader = new ytdl::YouTubeDownloader(m_moduleDirectory, this);

        RunAttempt();
    }

    /** Called on the UI thread after an attempt produced no file. */
    void NextAttempt()
    {
        if (m_done || !m_downloader)
            return;

        if (m_attempt + 1 >= m_strategies.size())
        {
            m_done = true;
            ReportFailure();
            return;
        }

        ++m_attempt;
        RunAttempt();
    }

    /**
     * Called on the UI thread when there is nothing left to try.
     *
     * A failure that reads like a missing account is not really an error - it is
     * a missing step. So instead of an error box, offer the sign-in for the
     * service this link belongs to and let the user press Download again.
     */
    void ReportFailure()
    {
        const std::wstring host = ytdl::TargetHost(m_url);

        // Checked before the account case: this one masquerades as an auth
        // failure ("The page needs to be reloaded") but signing in cannot fix
        // it, so offering the sign-in would just be a loop.
        if (NSYoutube::LooksLikeMissingJsRuntime(m_log))
        {
            const std::wstring text =
                tr(L"Could not download this video.") + L"\n\n" +
                tr(L"The component that reads YouTube video links is missing. "
                   L"Please reinstall the plugin.");

            ::MessageBoxW(m_hWnd, text.c_str(), tr(L"Error").c_str(), MB_OK | MB_ICONWARNING);
            return;
        }

        if (!host.empty() && NSYoutube::LooksLikeSignInRequired(m_log))
        {
            NSUI::ShowLoginDialog(m_hWnd, m_workDirectory, host);
            return;
        }

        std::wstring text = tr(L"Could not download this video.");

        if (!m_lastError.empty())
            text += L"\n\n" + NSStringUtils::utf8_to_wstring(m_lastError);

        ::MessageBoxW(m_hWnd, text.c_str(), tr(L"Error").c_str(), MB_OK | MB_ICONWARNING);
    }

private:
    void AppendLog(const std::string& message)
    {
        // Enough to recognise the failure, bounded so a long download cannot
        // grow this without limit.
        if (m_log.size() < 32768)
            m_log += message + "\n";

        if (message.compare(0, 6, "ERROR:") == 0)
            m_lastError = message;
    }

    void RunAttempt()
    {
        ClearTempDirectory();
        m_log.clear();
        m_lastError.clear();
        m_path.clear();

        ytdl::DownloadOptions options;
        options.quality = ytdl::Quality::P360;
        options.format = ytdl::Format::MP4;
        options.outputPath = m_workDirectoryTemp;
        options.outputTemplate = L"%(title)s.%(ext)s";

        SetCaption(tr(L"Downloading..."));

        m_downloader->download(m_url, options, m_strategies[m_attempt]);
    }

    void ClearTempDirectory()
    {
        ::CreateDirectoryW(m_workDirectoryTemp.c_str(), NULL);

        // A failed attempt leaves .part and .ytdl files behind, and
        // RemoveDirectory refuses to delete a directory that is not empty.
        WIN32_FIND_DATAW findData;
        HANDLE hFind = ::FindFirstFileW((m_workDirectoryTemp + L"\\*").c_str(), &findData);

        if (hFind == INVALID_HANDLE_VALUE)
            return;

        do
        {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            ::DeleteFileW((m_workDirectoryTemp + L"\\" + findData.cFileName).c_str());
        }
        while (::FindNextFileW(hFind, &findData));

        ::FindClose(hFind);
    }

    /**
     * True for the per-format files yt-dlp downloads before merging. If the
     * merge failed those are left behind, and picking one up would hand the
     * host a video with no audio - or, worse, audio with no video.
     *
     * Both spellings occur: "Title.f137.mp4" and "Title.f140-1.m4a", the latter
     * when YouTube offers several variants of one format id.
     */
    static bool LooksLikeIntermediate(const std::wstring& name)
    {
        const size_t dot = name.find_last_of(L'.');
        if (dot == std::wstring::npos || dot == 0)
            return false;

        const size_t previous = name.find_last_of(L'.', dot - 1);
        if (previous == std::wstring::npos || previous + 2 >= dot)
            return false;

        if (name[previous + 1] != L'f')
            return false;

        bool digit = false;

        for (size_t i = previous + 2; i < dot; ++i)
        {
            if (name[i] >= L'0' && name[i] <= L'9')
                digit = true;
            else if (name[i] != L'-')
                return false;
        }

        return digit;
    }

    /**
     * Is this a finished media file?
     *
     * Not every service ends up as mp4 - Twitch, Vimeo and others can produce
     * mkv or webm depending on what the site publishes - so accept any media
     * container and let the host deal with it. ".part" and ".ytdl" are yt-dlp's
     * work in progress and must never be picked up.
     */
    static bool IsFinishedMedia(const std::wstring& name)
    {
        static const wchar_t* extensions[] =
        {
            L".mp4", L".mkv", L".webm", L".mov", L".flv", L".avi", L".ts",
            L".m4a", L".mp3", L".opus", L".ogg", L".wav", L".aac", L".flac"
        };

        const size_t dot = name.find_last_of(L'.');
        if (dot == std::wstring::npos)
            return false;

        const std::wstring extension = name.substr(dot);

        for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i)
        {
            if (_wcsicmp(extension.c_str(), extensions[i]) == 0)
                return true;
        }

        return false;
    }

    /** Moves the finished download out of the temp directory. Empty if there is none. */
    std::wstring FindDownloadedFile()
    {
        WIN32_FIND_DATAW findData;
        HANDLE hFind = ::FindFirstFileW((m_workDirectoryTemp + L"\\*").c_str(), &findData);

        if (hFind == INVALID_HANDLE_VALUE)
            return L"";

        std::wstring name;

        do
        {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            if (!IsFinishedMedia(findData.cFileName))
                continue;

            if (LooksLikeIntermediate(findData.cFileName))
                continue;

            // Prefer mp4 when yt-dlp left more than one file behind.
            name = findData.cFileName;

            const size_t dot = name.find_last_of(L'.');
            if (dot != std::wstring::npos && _wcsicmp(name.substr(dot).c_str(), L".mp4") == 0)
                break;
        }
        while (::FindNextFileW(hFind, &findData));

        ::FindClose(hFind);

        if (name.empty())
            return L"";

        // yt-dlp names the file after the video title and only removes what the
        // filesystem forbids, so hashtag clouds and line breaks survive into the
        // name. Clean it on the way out of the temp directory.
        std::wstring stem = name;
        std::wstring extension;

        const size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos)
        {
            stem = name.substr(0, dot);
            extension = name.substr(dot);
        }

        // Not localised on purpose: a file name should not change with the UI
        // language. The video id is preferred anyway, and it always identifies.
        std::wstring fallback = NSYoutube::VideoIdFromUrl(m_url);
        if (fallback.empty())
            fallback = L"video";

        const std::wstring source = m_workDirectoryTemp + L"\\" + name;
        const std::wstring target = m_workDirectory + L"\\" +
                                    NSYoutube::CleanFileStem(stem, fallback) + extension;

        ::MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
        return target;
    }

};

namespace NSUI
{
    // ======================================================================
    // Sign-in dialog
    // ----------------------------------------------------------------------
    // Shown only when a download failed for want of an account. It names the
    // service, opens the browser on OK, then waits with both buttons disabled
    // until that browser is closed - which is also the moment the cookies are
    // flushed to disk.
    // ======================================================================

    struct LoginContext
    {
        std::wstring workDirectory;
        std::wstring service;
        std::wstring host;

        HWND   hText = NULL;
        HWND   hIcon = NULL;
        HWND   hOk = NULL;
        HWND   hCancel = NULL;
        HFONT  font = NULL;
        HANDLE process = NULL;
        bool   signedIn = false;
    };

    void CenterWindow(HWND hwnd);

    LRESULT CALLBACK LoginWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE)
        {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        LoginContext* ctx = (LoginContext*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (!ctx)
            return DefWindowProc(hwnd, msg, wParam, lParam);

        switch (msg)
        {
        case WM_CREATE:
        {
            const std::wstring message =
                tr(L"This video needs a signed-in %1 account.", ctx->service) + L"\r\n\r\n" +
                tr(L"Press OK to open a browser and sign in.");

            // Alert icon on the left, the way a standard dialog is laid out.
            ctx->hIcon = CreateWindowW(L"STATIC", NULL,
                WS_CHILD | WS_VISIBLE | SS_ICON | SS_REALSIZEIMAGE,
                18, 18, 32, 32, hwnd, NULL, GetModuleHandle(NULL), NULL);

            SendMessageW(ctx->hIcon, STM_SETICON,
                         (WPARAM)LoadIconW(NULL, IDI_INFORMATION), 0);

            ctx->hText = CreateWindowW(L"STATIC", message.c_str(),
                WS_CHILD | WS_VISIBLE,
                64, 16, 360, 72, hwnd, NULL, GetModuleHandle(NULL), NULL);

            ctx->hOk = CreateWindowW(L"BUTTON", tr(L"OK").c_str(),
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                266, 100, 76, 26, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);

            ctx->hCancel = CreateWindowW(L"BUTTON", tr(L"Cancel").c_str(),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                350, 100, 76, 26, hwnd, (HMENU)2, GetModuleHandle(NULL), NULL);

            // The shell's own message font, not DEFAULT_GUI_FONT - that one is
            // still the 1990s bitmap face and looks dead next to the host app.
            NONCLIENTMETRICSW metrics;
            memset(&metrics, 0, sizeof(metrics));
            metrics.cbSize = sizeof(metrics);

            HFONT font = NULL;
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
                font = CreateFontIndirectW(&metrics.lfMessageFont);

            if (!font)
                font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            else
                ctx->font = font;          // ours to destroy

            SendMessage(ctx->hText, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(ctx->hOk, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(ctx->hCancel, WM_SETFONT, (WPARAM)font, TRUE);
            break;
        }

        case WM_CTLCOLORSTATIC:
            // Without this a STATIC paints itself with COLOR_BTNFACE, which
            // shows up as a dull grey slab on the window's white background.
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case 1: // OK - open the browser and start waiting
            {
                const ytdl::LoginBrowser browser = ytdl::FindLoginBrowser();

                if (!browser.Valid() ||
                    (ctx->process = ytdl::StartLogin(browser, ctx->workDirectory,
                                                     ytdl::SignInUrl(ctx->host))) == NULL)
                {
                    ::MessageBoxW(hwnd, tr(L"No supported browser was found for signing in.").c_str(),
                                  tr(L"Error").c_str(), MB_OK | MB_ICONWARNING);
                    break;
                }

                EnableWindow(ctx->hOk, FALSE);
                EnableWindow(ctx->hCancel, FALSE);

                SetWindowTextW(ctx->hText,
                    tr(L"Waiting for the sign-in. Sign in in the browser window that just "
                       L"opened, then close it. This window will close by itself.").c_str());

                SetTimer(hwnd, YT_LOGIN_TIMER_ID, YT_LOGIN_POLL_MS, NULL);
                break;
            }
            case 2: // Cancel
                DestroyWindow(hwnd);
                break;
            default:
                break;
            }
            break;

        case WM_TIMER:
            if (wParam == YT_LOGIN_TIMER_ID && ctx->process)
            {
                if (WaitForSingleObject(ctx->process, 0) == WAIT_OBJECT_0)
                {
                    KillTimer(hwnd, YT_LOGIN_TIMER_ID);
                    CloseHandle(ctx->process);
                    ctx->process = NULL;

                    // The browser flushes its cookie database on a clean exit,
                    // so this is the first moment the result can be checked.
                    ctx->signedIn = ytdl::LoginProfileHasCookies(ctx->workDirectory);
                    DestroyWindow(hwnd);
                }
            }
            break;

        case WM_CLOSE:
            // While the browser is up its profile is locked, so a download
            // could not use it anyway. Wait for it rather than half-finish.
            if (ctx->process)
                return 0;

            DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            KillTimer(hwnd, YT_LOGIN_TIMER_ID);

            if (ctx->process)
            {
                CloseHandle(ctx->process);
                ctx->process = NULL;
            }

            if (ctx->font)
            {
                DeleteObject(ctx->font);
                ctx->font = NULL;
            }

            // Wake the modal loop below so it notices the window is gone. Not
            // PostQuitMessage: that WM_QUIT would also end the main window's
            // loop and close the whole plugin.
            PostThreadMessage(GetCurrentThreadId(), WM_NULL, 0, 0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        return 0;
    }

    /** Modal. Returns true when a sign-in was completed and stored. */
    bool ShowLoginDialog(HWND parent, const std::wstring& workDirectory, const std::wstring& host)
    {
        LoginContext ctx;
        ctx.workDirectory = workDirectory;
        ctx.host = host;
        ctx.service = ytdl::ServiceName(host);

        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = LoginWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"YoutubeLoginWindowClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

        // Same icon as the main plugin window; without this the title bar gets
        // the blank default.
        const std::wstring iconPath = workDirectory + L"\\icon_internal.ico";
        wc.hIcon = (HICON)LoadImageW(NULL, iconPath.c_str(), IMAGE_ICON, 32, 32,
                                     LR_LOADFROMFILE | LR_DEFAULTCOLOR);
        wc.hIconSm = (HICON)LoadImageW(NULL, iconPath.c_str(), IMAGE_ICON, 16, 16,
                                       LR_LOADFROMFILE | LR_DEFAULTCOLOR);

        RegisterClassExW(&wc);

        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        RECT rc = { 0, 0, 440, 140 };
        AdjustWindowRectEx(&rc, style, FALSE, 0);

        HWND hwnd = CreateWindowExW(0, wc.lpszClassName, tr(L"Sign in required").c_str(), style,
                                    CW_USEDEFAULT, CW_USEDEFAULT,
                                    rc.right - rc.left, rc.bottom - rc.top,
                                    parent, NULL, GetModuleHandle(NULL), &ctx);
        if (!hwnd)
            return false;

        CenterWindow(hwnd);
        EnableWindow(parent, FALSE);
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        MSG msg;
        while (IsWindow(hwnd) && GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        EnableWindow(parent, TRUE);
        SetActiveWindow(parent);

        return ctx.signedIn;
    }

    // ======================================================================
    // About
    // ======================================================================

    void ShowAboutDialog(HWND parent)
    {
        const std::wstring text =
            tr(L"Supported: YouTube and Twitch. About 1750 other sites are handled by "
               L"yt-dlp and usually work, but are not tested.") + L"\r\n\r\n" +
            tr(L"Some videos need an account. The plugin will offer to sign in when "
               L"that happens.") + L"\r\n\r\n" +
            tr(L"Services protected by DRM cannot be downloaded.") + L"\r\n\r\n" +
            L"yt-dlp " YT_DLP_BUNDLED_VERSION;

        ::MessageBoxW(parent, text.c_str(), tr(L"About").c_str(), MB_OK | MB_ICONINFORMATION);
    }

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
        HWND hBtnOK, hBtnCancel, hDownload, hAbout;

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

            hDownload = CreateWindow(L"BUTTON", tr(L"Download").c_str(),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                390, 10, 100, 25, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);

            hBtnOK = CreateWindow(L"BUTTON", tr(L"OK").c_str(),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                180, 55, 60, 25, hwnd, (HMENU)2, GetModuleHandle(NULL), NULL);

            hBtnCancel = CreateWindow(L"BUTTON", tr(L"Cancel").c_str(),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                260, 55, 60, 25, hwnd, (HMENU)3, GetModuleHandle(NULL), NULL);

            // There is deliberately no sign-in button: signing in is offered by
            // the download when a video actually turns out to need an account.
            hAbout = CreateWindow(L"BUTTON", tr(L"About").c_str(),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                10, 55, 80, 25, hwnd, (HMENU)5, GetModuleHandle(NULL), NULL);

            HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            SendMessage(hEdit, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(hDownload, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(hBtnOK, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(hBtnCancel, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(hAbout, WM_SETFONT, (WPARAM)font, TRUE);

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
            case 5: // About
            {
                NSUI::ShowAboutDialog(hwnd);
                break;
            }
            default:
                break;
            }
            break;
        case WM_YT_NEXT_ATTEMPT:
            // The previous cookie source produced nothing; move to the next one.
            plugin->NextAttempt();
            break;

        case WM_YT_FAILED:
            plugin->ReportFailure();
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


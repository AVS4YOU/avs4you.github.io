// dllmain.cpp : Defines the entry point for the DLL application.
#define PLUGIN_EXPORTS
#include <Windows.h>
#include "ShlObj.h"

#include "../../sdk/include/CContentPluginIntf.h"
#include "../../sdk/include/AVSConsts.h"
#include "../../sdk/translate/translate.h"
#include "../../sdk/common/HostProcess.h"
#include "../../sdk/common/utils.h"
#include "../../sdk/ui/winapi/ui.h"

#include "./src/yt_downloader.h"
#include "./src/failure_reason.h"
#include "./src/output_name.h"
#include "./src/output_folder.h"
#include "./src/media_toolkit_support.h"
#include "resource.h"

#include <cctype>
#include <cstring>
#include <algorithm>
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
#define WM_YT_FORMATS_READY (WM_APP + 3)
#define WM_YT_COMPLETE     (WM_APP + 4)
#define WM_YT_NEXT_FORMAT_ATTEMPT (WM_APP + 5)
#define WM_YT_RETRY_FORMATS       (WM_APP + 6)

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
    void ShowSettingsDialog(HWND parent, CYoutubePlugin* plugin);
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

    // Where finished files go: Videos\YouTube or the folder chosen in
    // Settings (src/output_folder.h).
    std::wstring m_outputFolder;

    std::wstring m_path = L"";

    std::wstring m_moduleDirectory;

    HWND m_hWnd = NULL;
    HWND m_parentWindow = NULL;
    bool m_isAudioHost = false;      // Audio Editor or Audio Converter: audio only

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
    std::wstring                       m_videoTitle;
    bool                               m_done = false;
    bool                               m_downloadInProgress = false;

    // Set when the download worked but the file could not be moved into the
    // output folder: that is reported as such, not as a failed download.
    std::wstring                       m_saveFailedFolder;
    DWORD                              m_saveError = ERROR_SUCCESS;

    // A check is a separate yt-dlp operation. Its output is used only to fill
    // the two selectors in the window; it never creates a media file.
    bool                               m_listingFormats = false;
    bool                               m_checkingFormats = false;
    bool                               m_formatsAvailable = false;
    std::wstring                       m_checkedUrl;
    std::vector<int>                   m_availableQualities;
    std::vector<std::wstring>          m_availableVideoCodecs;
    bool                               m_canChooseStreamType = false;
    int                                m_selectedQuality = 0;
    int                                m_streamMode = 0; // 0: video+audio, 1: video, 2: audio
    std::wstring                       m_selectedVideoCodec;
    std::wstring m_selectedAudioLanguage;
    std::vector<std::pair<std::wstring, std::wstring>> m_audioTracks;

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
        m_outputFolder = ytdl::LoadOutputFolder(m_workDirectory);

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

    const std::wstring& OutputFolder() const
    {
        return m_outputFolder;
    }

    /** Remembers a folder chosen in Settings. False when it could not be stored. */
    bool SetOutputFolder(const std::wstring& folder)
    {
        if (!ytdl::SaveOutputFolder(m_workDirectory, folder))
            return false;

        m_outputFolder = ytdl::LoadOutputFolder(m_workDirectory);
        return true;
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
            if (m_listingFormats)
            {
                m_listingFormats = false;
                m_availableQualities = QualitiesFromFormatList(m_log);
                m_availableVideoCodecs = VideoCodecsFromFormatList(m_log);
                m_audioTracks = AudioTracksFromFormatList(m_log);
                m_canChooseStreamType = HasSeparateVideoAndAudio(m_log);
                m_formatsAvailable = !m_availableQualities.empty();

                if (!m_formatsAvailable && m_attempt + 1 < m_strategies.size() &&
                    NSYoutube::WorthAnotherCookieSource(m_log))
                {
                    if (m_hWnd)
                        PostMessageW(m_hWnd, WM_YT_NEXT_FORMAT_ATTEMPT, 0, 0);
                    break;
                }

                if (m_formatsAvailable)
                    m_checkingFormats = false;

                if (m_hWnd)
                    PostMessageW(m_hWnd, WM_YT_FORMATS_READY, 0, 0);
                break;
            }

            m_path = message == "0" ? FindDownloadedFile() : L"";

            if (!m_path.empty() && NSSystemUtils::ExistsFile(m_path))
            {
                m_done = true;
                m_downloadInProgress = false;
                SetCaption(tr(L"Complete"));
                if (m_hWnd)
                    PostMessageW(m_hWnd, WM_YT_COMPLETE, 0, 0);
            }
            else if (!m_saveFailedFolder.empty())
            {
                // Downloaded, but the output folder took nothing. Another cookie
                // source would only download the same file again.
                m_done = true;
                m_downloadInProgress = false;
                SetCaption(tr(L"Error"));
                if (m_hWnd)
                    PostMessageW(m_hWnd, WM_YT_FAILED, 0, 0);
            }
            else if (m_attempt + 1 < m_strategies.size() &&
                m_log.find("AVSMediaToolkit") == std::string::npos && NSYoutube::WorthAnotherCookieSource(m_log))
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
        m_downloadInProgress = false;
        if (m_downloader)
            delete m_downloader;
        m_downloader = nullptr;
    }

    void Start(const std::wstring& url, int quality, int streamMode,
               const std::wstring& videoCodec, const std::wstring& audioLanguage)
    {
        Stop();

        m_url = url;
        m_strategies = ytdl::BuildCookieStrategies(m_workDirectory);
        m_attempt = 0;
        m_done = false;
        m_downloadInProgress = true;
        m_selectedQuality = quality;
        m_streamMode = streamMode;
        m_selectedVideoCodec = videoCodec;
        m_selectedAudioLanguage = audioLanguage;

        m_downloader = new ytdl::YouTubeDownloader(m_moduleDirectory, this);

        RunAttempt();
    }

    /** Starts the non-destructive `yt-dlp -F` check used by the UI. */
    void CheckFormats(const std::wstring& url)
    {
        Stop();
        m_url = url;
        m_checkedUrl = url;
        m_strategies = ytdl::BuildCookieStrategies(m_workDirectory);
        m_attempt = 0;
        m_log.clear();
        m_lastError.clear();
        m_videoTitle.clear();
        m_formatsAvailable = false;
        m_availableQualities.clear();
        m_availableVideoCodecs.clear();
        m_audioTracks.clear();
        m_canChooseStreamType = false;
        m_downloadInProgress = false;
        m_checkingFormats = true;
        m_listingFormats = true;

        m_downloader = new ytdl::YouTubeDownloader(m_moduleDirectory, this);
        SetCaption(tr(L"Checking formats..."));
        m_downloader->listFormats(url, m_strategies[m_attempt]);
    }

    void NextFormatAttempt()
    {
        if (!m_checkingFormats || !m_downloader || m_attempt + 1 >= m_strategies.size())
            return;

        ++m_attempt;
        m_log.clear();
        m_lastError.clear();
        m_videoTitle.clear();
        m_listingFormats = true;
        m_downloader->listFormats(m_url, m_strategies[m_attempt]);
    }

    void RetryFormatsAfterLogin()
    {
        if (!m_url.empty())
            CheckFormats(m_url);
    }

    bool HasFormatsFor(const std::wstring& url) const
    {
        return m_formatsAvailable && m_checkedUrl == url;
    }

    bool IsDownloadInProgress() const
    {
        return m_downloadInProgress;
    }

    const std::vector<int>& AvailableQualities() const
    {
        return m_availableQualities;
    }

    const std::wstring& VideoTitle() const
    {
        return m_videoTitle;
    }

    const std::vector<std::wstring>& AvailableVideoCodecs() const
    {
        return m_availableVideoCodecs;
    }

    bool CanChooseStreamType() const
    {
        return m_canChooseStreamType;
    }

    bool IsYouTubeUrl() const
    {
        return ytdl::IsYouTubeHost(ytdl::TargetHost(m_url));
    }

    void SetHostWindow(HWND hwnd)
    {
        m_parentWindow = hwnd && IsWindow(hwnd) ? GetAncestor(hwnd, GA_ROOT) : NULL;
        // The audio products take the audio track only: no picture, so no
        // quality, codec or stream choice, and no AVS video DLLs needed.
        const std::wstring host = NSHostProcess::GetHostProcess();
        m_isAudioHost = m_parentWindow &&
            (host.find(L"avsaudioeditor") != std::wstring::npos ||
             host.find(L"avsaudioconverter") != std::wstring::npos);
    }

    bool IsAudioHost() const
    {
        return m_isAudioHost;
    }

    void InvalidateCheckedFormats()
    {
        m_formatsAvailable = false;
        m_checkedUrl.clear();
        m_availableQualities.clear();
        m_availableVideoCodecs.clear();
        m_audioTracks.clear();
        m_canChooseStreamType = false;
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
        if (!m_saveFailedFolder.empty())
        {
            std::wstring text = tr(L"The video was downloaded but could not be saved to %1.", m_saveFailedFolder);

            wchar_t* reason = NULL;
            if (::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS, NULL, m_saveError, 0, (LPWSTR)&reason, 0, NULL) &&
                reason)
            {
                std::wstring line = reason;
                ::LocalFree(reason);
                line.erase(line.find_last_not_of(L" \r\n") + 1);
                text += L"\n\n" + line;
            }

            text += L"\n\n" + tr(L"Choose another folder in Settings.");
            ::MessageBoxW(m_hWnd, text.c_str(), tr(L"Error").c_str(), MB_OK | MB_ICONWARNING);
            return;
        }

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

        if (m_log.find("AVSMediaToolkit") == std::string::npos &&
            !host.empty() && NSYoutube::LooksLikeSignInRequired(m_log))
        {
            const bool signedIn = NSUI::ShowLoginDialog(m_hWnd, m_workDirectory, host);
            if (signedIn && m_checkingFormats && m_hWnd)
                PostMessageW(m_hWnd, WM_YT_RETRY_FORMATS, 0, 0);
            return;
        }

        std::wstring text = tr(L"Could not download this video.");

        if (!m_lastError.empty())
            text += L"\n\n" + NSStringUtils::utf8_to_wstring(m_lastError);

        ::MessageBoxW(m_hWnd, text.c_str(), tr(L"Error").c_str(), MB_OK | MB_ICONWARNING);
    }

private:
    /** Extracts the heights from `yt-dlp --list-formats` table rows. */
    static std::vector<int> QualitiesFromFormatList(const std::string& log)
    {
        std::vector<int> result;
        size_t lineStart = 0;

        while (lineStart < log.size())
        {
            const size_t lineEnd = log.find('\n', lineStart);
            const std::string line = log.substr(lineStart, lineEnd - lineStart);

            // `sb0`-`sb3` are mhtml storyboard images, not downloadable video
            // formats. Their dimensions (for example 320x180) must never be
            // exposed as video quality options.
            if (NSYoutube::Mentions(line, "storyboard") ||
                NSYoutube::Mentions(line, "mhtml") ||
                NSYoutube::Mentions(line, "| images"))
            {
                if (lineEnd == std::string::npos)
                    break;
                lineStart = lineEnd + 1;
                continue;
            }

            int height = 0;

            // YouTube prints resolutions as 1920x1080. Requiring digits on
            // both sides avoids treating format ids and bitrates as a quality.
            for (size_t x = 1; x + 1 < line.size(); ++x)
            {
                if (line[x] != 'x' || !isdigit((unsigned char)line[x - 1]) ||
                    !isdigit((unsigned char)line[x + 1]))
                    continue;

                size_t begin = x;
                while (begin > 0 && isdigit((unsigned char)line[begin - 1]))
                    --begin;
                size_t end = x + 1;
                while (end < line.size() && isdigit((unsigned char)line[end]))
                    ++end;

                height = atoi(line.substr(x + 1, end - x - 1).c_str());
                break;
            }

            // Twitch names the same thing simply "360p" or "1080p". It
            // supplies no width, so accept that notation as well.
            if (height == 0)
            {
                for (size_t p = 1; p < line.size(); ++p)
                {
                    if (line[p] != 'p' || !isdigit((unsigned char)line[p - 1]))
                        continue;

                    size_t begin = p;
                    while (begin > 0 && isdigit((unsigned char)line[begin - 1]))
                        --begin;
                    height = atoi(line.substr(begin, p - begin).c_str());
                    break;
                }
            }

            if (height >= 100 && height <= 10000 &&
                std::find(result.begin(), result.end(), height) == result.end())
                result.push_back(height);

            if (lineEnd == std::string::npos)
                break;
            lineStart = lineEnd + 1;
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    // Group encoding/bitrate variants by the language reported in MORE INFO.
    static std::vector<std::pair<std::wstring, std::wstring>> AudioTracksFromFormatList(const std::string& log)
    {
        std::vector<std::pair<std::wstring, std::wstring>> tracks;
        std::istringstream input(log);
        std::string line;
        while (std::getline(input, line))
        {
            if (line.find("audio only") == std::string::npos) continue;
            const auto begin = line.find('[');
            const auto end = line.find(']', begin);
            if (begin == std::string::npos || end == std::string::npos) continue;
            const std::string code = line.substr(begin + 1, end - begin - 1);
            if (code.empty() || !std::all_of(code.begin(), code.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '-'; })) continue;
            const auto language = NSStringUtils::utf8_to_wstring(code);
            if (std::any_of(tracks.begin(), tracks.end(), [&](const std::pair<std::wstring, std::wstring>& track) {
                return track.first == language; })) continue;
            std::string label = line.substr(end + 1);
            const auto comma = label.find(',');
            if (comma != std::string::npos) label.resize(comma);
            const auto first = label.find_first_not_of(" \r\t");
            label = first == std::string::npos ? code : label.substr(first);
            tracks.emplace_back(language, L"[" + language + L"] " + NSStringUtils::utf8_to_wstring(label));
        }
        return tracks;
    }

    /** Codec families reported by yt-dlp in the VCODEC column. */
    static std::vector<std::wstring> VideoCodecsFromFormatList(const std::string& log)
    {
        struct Codec { const char* marker; const wchar_t* selector; };
        static const Codec codecs[] =
        {
            { "avc1", L"avc1" }, { "hvc1", L"hvc1" }, { "hev1", L"hev1" },
            { "av01", L"av01" },
            { "vp8", L"vp8" }, { "theora", L"theora" }
        };

        std::vector<std::wstring> result;
        // yt-dlp uses both the short WebM name (vp9) and the ISO codec string
        // (vp09). They describe the same codec, so exposing both in the UI is
        // a confusing duplicate.
        if (NSYoutube::Mentions(log, "vp09") || NSYoutube::Mentions(log, "vp9"))
            result.push_back(L"VP9");

        for (size_t i = 0; i < sizeof(codecs) / sizeof(codecs[0]); ++i)
        {
            if (NSYoutube::Mentions(log, codecs[i].marker))
                result.push_back(codecs[i].selector);
        }
        return result;
    }

    /** True only when the service exposes both separate video and audio tracks. */
    static bool HasSeparateVideoAndAudio(const std::string& log)
    {
        return NSYoutube::Mentions(log, "video only") &&
               NSYoutube::Mentions(log, "audio only");
    }

    void AppendLog(const std::string& message)
    {
        static const std::string marker = "__YT_TITLE__:";
        if (message.compare(0, marker.size(), marker) == 0)
            m_videoTitle = NSStringUtils::utf8_to_wstring(message.substr(marker.size()));

        // Enough to recognise the failure, bounded so a long download cannot
        // grow this without limit.
        if (m_log.size() < (m_listingFormats ? 1024 * 1024 : 32768))
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
        m_saveFailedFolder.clear();
        m_saveError = ERROR_SUCCESS;

        ytdl::DownloadOptions options;
        options.format = ytdl::Format::MP4;
        options.outputPath = m_workDirectoryTemp;
        options.outputTemplate = L"%(title)s.%(ext)s";

        const std::wstring cap = m_selectedQuality > 0
            ? L"[height<=" + std::to_wstring(m_selectedQuality) + L"]" : L"";
        std::wstring codec;
        if (m_selectedVideoCodec == L"VP9")
            // The optional zero covers both `vp9` and `vp09` spellings.
            codec = L"[vcodec~='(?i)^vp0?9']";
        else if (!m_selectedVideoCodec.empty())
            codec = L"[vcodec^=" + m_selectedVideoCodec + L"]";
        const std::wstring audio = m_selectedAudioLanguage.empty() ? L""
            : L"[language=" + m_selectedAudioLanguage + L"]";
        if (m_streamMode == 2)
        {
            options.extractAudio = true;
            options.formatSelector = L"ba" + audio;
        }
        else if (m_streamMode == 1)
        {
            // YouTube-only video track: never add an audio format.
            options.formatSelector = L"bv*" + codec + cap + L"/bv*" + cap;
        }
        else
        {
            // Prefer separate tracks for the selected height: this is the
            // usual way to get the highest quality a service offers.
            options.formatSelector = L"bv" + codec + cap + L"[ext=mp4]+ba[ext=m4a]" + audio
                + L"/bv" + codec + cap + L"[ext=mp4]+ba[ext=mp4]" + audio
                + L"/bv" + codec + cap + L"[ext=webm]+ba[ext=webm][acodec=opus]" + audio
                + L"/b" + codec + cap + audio + L"/b" + audio;
        }
        if (m_streamMode == 3)
        {
            // Some services only expose ready-made (progressive) files.
            options.formatSelector = L"b" + codec + cap + audio + L"/b" + cap + audio + L"/b" + audio;
        }

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

    /**
     * Moves the finished download out of the temp directory into the output
     * folder. Empty if there is none, or if it could not be moved - then
     * m_saveFailedFolder says where it was meant to go.
     */
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

        // Across drives this is a copy, which a long video makes noticeable.
        SetCaption(tr(L"Saving..."));

        // Never over a file that is already there: the folder is the user's own.
        const std::wstring source = m_workDirectoryTemp + L"\\" + name;
        const std::wstring folder = m_outputFolder;
        DWORD error = ERROR_SUCCESS;
        const std::wstring target = ytdl::MoveIntoFolder(source, folder,
                                                         NSYoutube::CleanFileStem(stem, fallback), extension, error);
        if (target.empty())
        {
            m_saveFailedFolder = folder;
            m_saveError = error;
        }
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

    // ----------------------------------------------------------------------
    // The AVS widgets every plugin window is made of (sdk/ui/winapi): flat
    // buttons, edits, labels and combo boxes on the AVS window background.
    // ----------------------------------------------------------------------

    HBRUSH WindowBackground()
    {
        static HBRUSH brush = NULL;
        if (!brush)
        {
            const AVS::Color color = AVS::Color::GetDefaultWindowBackground();
            brush = CreateSolidBrush(RGB(color.R, color.G, color.B));
        }
        return brush;
    }

    HWND Button(HWND parent, int id, const std::wstring& text, int x, int y, int width, int height,
                AVS::Buttons type = AVS::Buttons::Default)
    {
        return AVS::CreateButton(parent, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), text.c_str(),
                                 x, y, width, height, AVS::ButtonSettings::Create(type));
    }

    HWND Label(HWND parent, const std::wstring& text, int x, int y, int width, int height,
               AVS::LabelType type = AVS::LabelType::Disabled,
               DWORD alignment = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
    {
        return AVS::CreateLabel(parent, GetModuleHandle(NULL), text.c_str(), x, y, width, height,
                                AVS::LabelSettings::Create(type, alignment));
    }

    /** One line of text in the middle of its box; notifies its parent as control 0. */
    HWND LineEdit(HWND parent, const std::wstring& text, int x, int y, int width, int height,
                  const std::wstring& placeholder = L"")
    {
        AVS::TextEditSettings settings = AVS::TextEditSettings::Create();
        settings.IsVCenter = true;
        settings.Placeholder = placeholder;
        HWND edit = AVS::CreateTextEditMultiline(parent, GetModuleHandle(NULL), x, y, width, height, settings);
        SetWindowTextW(edit, text.c_str());
        return edit;
    }

    HWND Combo(HWND parent, int id, int x, int y, int width, int height)
    {
        return AVS::CreateComboBox(parent, (HMENU)(INT_PTR)id, GetModuleHandle(NULL),
                                   x, y, width, height, AVS::ComboBoxSettings::Create());
    }

    struct LoginContext
    {
        std::wstring workDirectory;
        std::wstring service;
        std::wstring host;

        HWND   hText = NULL;
        HWND   hIcon = NULL;
        HWND   hOk = NULL;
        HWND   hCancel = NULL;
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

            ctx->hText = Label(hwnd, message, 64, 16, 360, 72, AVS::LabelType::Enabled,
                               DT_LEFT | DT_TOP | DT_WORDBREAK);
            ctx->hOk = Button(hwnd, 1, tr(L"OK"), 235, 96, 90, 26, AVS::Buttons::Primary);
            ctx->hCancel = Button(hwnd, 2, tr(L"Cancel"), 335, 96, 90, 26);
            break;
        }

        case WM_CTLCOLORSTATIC:
            // The icon on the window's own background, not COLOR_BTNFACE.
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (LRESULT)WindowBackground();
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

                AVS::Label_SetText(ctx->hText,
                    tr(L"Waiting for the sign-in. Sign in in the browser window that just "
                       L"opened, then close it. This window will close by itself."));

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
        wc.hbrBackground = WindowBackground();

        // Same icon as the main plugin window; without this the title bar gets
        // the blank default.
        const std::wstring iconPath = workDirectory + L"\\icon_internal.ico";
        wc.hIcon = (HICON)LoadImageW(NULL, iconPath.c_str(), IMAGE_ICON, 32, 32,
                                     LR_LOADFROMFILE | LR_DEFAULTCOLOR);
        wc.hIconSm = (HICON)LoadImageW(NULL, iconPath.c_str(), IMAGE_ICON, 16, 16,
                                       LR_LOADFROMFILE | LR_DEFAULTCOLOR);

        RegisterClassExW(&wc);

        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        RECT rc = { 0, 0, 440, 137 };
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

    // ======================================================================
    // Settings
    // ----------------------------------------------------------------------
    // The folder downloads go to, kept on OK; and signing out, done at once.
    // Signing out forgets the plugin's sign-in profile and cookies.txt, so no
    // account is used until the next sign-in (src/output_folder.h).
    // ======================================================================

    struct SettingsContext
    {
        CYoutubePlugin* plugin = nullptr;
        std::wstring    folder;          // shown; stored on OK

        HWND  hFolder = NULL;
        HWND  hSignOut = NULL;
    };

    /** Folder picker starting at current, or at the nearest folder above it that exists. Empty if cancelled. */
    std::wstring PickFolder(HWND owner, const std::wstring& current)
    {
        std::wstring result;
        const HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        IFileOpenDialog* dialog = NULL;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        {
            DWORD options = 0;
            dialog->GetOptions(&options);
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            dialog->SetTitle(tr(L"Choose a folder for downloaded files").c_str());

            std::wstring start = current;
            while (!start.empty() && GetFileAttributesW(start.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                const size_t slash = start.find_last_of(L"\\/");
                start = (slash == std::wstring::npos) ? std::wstring() : start.substr(0, slash);
            }

            IShellItem* item = NULL;
            if (!start.empty() && SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), NULL, IID_PPV_ARGS(&item))))
            {
                dialog->SetFolder(item);
                item->Release();
                item = NULL;
            }

            if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item)))
            {
                PWSTR path = NULL;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    result = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
            dialog->Release();
        }

        if (SUCCEEDED(init))
            CoUninitialize();
        return result;
    }

    LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE)
        {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        SettingsContext* ctx = (SettingsContext*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (!ctx)
            return DefWindowProc(hwnd, msg, wParam, lParam);

        switch (msg)
        {
        case WM_CREATE:
        {
            Label(hwnd, tr(L"Save downloaded files to:"), 15, 15, 430, 20);

            // Read-only: the folder is chosen with Browse, so it always exists.
            ctx->hFolder = LineEdit(hwnd, ctx->folder, 15, 37, 320, 28);
            SendMessageW(ctx->hFolder, EM_SETREADONLY, TRUE, 0);
            Button(hwnd, 3, tr(L"Browse..."), 345, 37, 100, 28);

            // Signing out on the left of the OK / Cancel row; off when there is
            // no account to forget.
            ctx->hSignOut = Button(hwnd, 4, tr(L"Sign out"), 15, 85, 130, 26);
            EnableWindow(ctx->hSignOut, ytdl::HasAccounts(ctx->plugin->m_workDirectory) ? TRUE : FALSE);

            HWND hOk = Button(hwnd, IDOK, tr(L"OK"), 255, 85, 90, 26, AVS::Buttons::Primary);
            Button(hwnd, IDCANCEL, tr(L"Cancel"), 355, 85, 90, 26);

            SetFocus(hOk);
            break;
        }

        case WM_CTLCOLORSTATIC:
            // The read-only path box white, as a field, not in COLOR_BTNFACE.
            if ((HWND)lParam == ctx->hFolder)
            {
                SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
                return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case 3: // Browse
            {
                const std::wstring folder = PickFolder(hwnd, ctx->folder);
                if (!folder.empty())
                {
                    ctx->folder = folder;
                    SetWindowTextW(ctx->hFolder, folder.c_str());
                }
                break;
            }
            case 4: // Sign out - at once, not on OK
            {
                if (MessageBoxW(hwnd, tr(L"Sign out of all accounts the plugin uses?").c_str(),
                                tr(L"Settings").c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES)
                    break;

                if (ytdl::SignOut(ctx->plugin->m_workDirectory))
                    EnableWindow(ctx->hSignOut, FALSE);
                else
                {
                    MessageBoxW(hwnd, tr(L"Could not sign out. Close the browser window used for "
                                         L"signing in and try again.").c_str(),
                                tr(L"Error").c_str(), MB_OK | MB_ICONWARNING);
                }
                break;
            }
            case IDOK:
                if (ctx->folder != ctx->plugin->OutputFolder() && !ctx->plugin->SetOutputFolder(ctx->folder))
                {
                    MessageBoxW(hwnd, tr(L"Could not save the settings.").c_str(), tr(L"Error").c_str(),
                                MB_OK | MB_ICONWARNING);
                    break;
                }
                DestroyWindow(hwnd);
                break;
            case IDCANCEL:
                DestroyWindow(hwnd);
                break;
            default:
                break;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            // Wakes the modal loop below; not PostQuitMessage, see the sign-in dialog.
            PostThreadMessage(GetCurrentThreadId(), WM_NULL, 0, 0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        return 0;
    }

    /** Modal. */
    void ShowSettingsDialog(HWND parent, CYoutubePlugin* plugin)
    {
        SettingsContext ctx;
        ctx.plugin = plugin;
        ctx.folder = plugin->OutputFolder();

        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"YoutubeSettingsWindowClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = WindowBackground();

        const std::wstring iconPath = plugin->m_workDirectory + L"\\icon_internal.ico";
        wc.hIcon = (HICON)LoadImageW(NULL, iconPath.c_str(), IMAGE_ICON, 32, 32,
                                     LR_LOADFROMFILE | LR_DEFAULTCOLOR);
        wc.hIconSm = (HICON)LoadImageW(NULL, iconPath.c_str(), IMAGE_ICON, 16, 16,
                                       LR_LOADFROMFILE | LR_DEFAULTCOLOR);

        RegisterClassExW(&wc);

        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        RECT rc = { 0, 0, 460, 126 };
        AdjustWindowRectEx(&rc, style, FALSE, 0);

        HWND hwnd = CreateWindowExW(0, wc.lpszClassName, tr(L"Settings").c_str(), style,
                                    CW_USEDEFAULT, CW_USEDEFAULT,
                                    rc.right - rc.left, rc.bottom - rc.top,
                                    parent, NULL, GetModuleHandle(NULL), &ctx);
        if (!hwnd)
            return;

        CenterWindow(hwnd);
        EnableWindow(parent, FALSE);
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        // IsDialogMessage gives Tab between the controls, Enter for OK and
        // Esc for Cancel.
        MSG msg;
        while (IsWindow(hwnd) && GetMessage(&msg, NULL, 0, 0))
        {
            if (!IsDialogMessageW(hwnd, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        EnableWindow(parent, TRUE);
        SetActiveWindow(parent);
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
        static HWND hAction, hAbout, hSettings, hQuality, hStream, hCodec, hAudio;

        // The link on top; under it, once checked, the format lists and - with
        // more than one audio track - the track list; the buttons last. Rows of
        // 28 px 10 apart, 15 px from the window's edges.
        const auto placeButtons = [&](int buttonY)
        {
            MoveWindow(hAbout, 15, buttonY, 110, 26, TRUE);
            MoveWindow(hSettings, 135, buttonY, 120, 26, TRUE);
            MoveWindow(hAction, 455, buttonY, 130, 26, TRUE);
            RECT rc = { 0, 0, 600, buttonY + 26 + 15 };
            AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
            SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                         SWP_NOMOVE | SWP_NOZORDER);
        };

        const auto showFormatSelectors = [&]()
        {
            ShowWindow(hQuality, SW_SHOW);
            ShowWindow(hStream, SW_SHOW);
            ShowWindow(hCodec, SW_SHOW);
            // "Default audio track" and one track are no choice
            const bool multipleAudio = AVS::ComboBox_GetItems(hAudio).size() > 2;
            ShowWindow(hAudio, multipleAudio ? SW_SHOW : SW_HIDE);
            placeButtons(multipleAudio ? 129 : 91);
        };

        const auto hideFormatSelectors = [&]()
        {
            ShowWindow(hQuality, SW_HIDE);
            ShowWindow(hStream, SW_HIDE);
            ShowWindow(hCodec, SW_HIDE);
            ShowWindow(hAudio, SW_HIDE);
            placeButtons(53);
        };

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
            // Control 0: its EN_CHANGE drops the formats of the previous link.
            // The example is a link, the same in every language.
            hEdit = LineEdit(hwnd, L"", 15, 15, 570, 28, L"https://www.youtube.com/watch?v=...");

            // In the tab order as on the screen; shown once the link is checked.
            hQuality = Combo(hwnd, 6, 15, 53, 180, 28);
            hStream = Combo(hwnd, 7, 205, 53, 150, 28);
            hCodec = Combo(hwnd, 8, 365, 53, 220, 28);
            hAudio = Combo(hwnd, 10, 15, 91, 570, 28);
            ShowWindow(hQuality, SW_HIDE);
            ShowWindow(hStream, SW_HIDE);
            ShowWindow(hCodec, SW_HIDE);
            ShowWindow(hAudio, SW_HIDE);

            // There is deliberately no sign-in button: signing in is offered by
            // the download when a video actually turns out to need an account.
            hAbout = Button(hwnd, 5, tr(L"About"), 15, 53, 110, 26);

            // The output folder and signing out. Disabled while a check or a
            // download runs: the folder is read when the file is saved, and
            // signing out would delete the profile yt-dlp is reading.
            hSettings = Button(hwnd, 9, tr(L"Settings"), 135, 53, 120, 26);

            hAction = Button(hwnd, 1, tr(L"Check"), 455, 53, 130, 26, AVS::Buttons::Primary);

            SetFocus(hEdit);
            plugin->m_hWnd = hwnd;
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case 0: // URL edit control changed: selections belong to the old URL.
                if (HIWORD(wParam) == EN_CHANGE)
                {
                    plugin->InvalidateCheckedFormats();
                    hideFormatSelectors();
                    SetWindowTextW(hAction, tr(L"Check").c_str());
                }
                break;
            case 7:
                if (HIWORD(wParam) == AVS::COMBOBOX_SELECTED_INDEX_CHANDED)
                {
                    // "Video only" has no audio track to choose
                    const int mode = AVS::ComboBox_GetCurrent(hStream);
                    EnableWindow(hAudio, !plugin->IsYouTubeUrl() || mode != 1);
                }
                break;
            case 1: // Check, Download, or Cancel depending on current state.
            {
                if (plugin->IsDownloadInProgress())
                {
                    plugin->Stop();
                    EnableWindow(hEdit, TRUE);
                    EnableWindow(hSettings, TRUE);
                    SetWindowTextW(hAction, tr(L"Download").c_str());
                    break;
                }

                int len = GetWindowTextLength(hEdit);
                if (len > 0)
                {
                    std::wstring url(len, L'\0');
                    GetWindowText(hEdit, &url[0], len + 1);

                    if (!plugin->HasFormatsFor(url))
                    {
                        hideFormatSelectors();
                        SetWindowTextW(hAction, tr(L"Check").c_str());
                        EnableWindow(hEdit, FALSE);
                        EnableWindow(hAction, FALSE);
                        EnableWindow(hSettings, FALSE);
                        plugin->CheckFormats(url);
                    }
                    else
                    {
                        const int qualityIndex = AVS::ComboBox_GetCurrent(hQuality);
                        const std::vector<int>& qualities = plugin->AvailableQualities();
                        const int quality = qualityIndex > 0 && (size_t)(qualityIndex - 1) < qualities.size()
                            ? qualities[qualityIndex - 1] : 0;
                        const int streamIndex = AVS::ComboBox_GetCurrent(hStream);
                        const int streamMode = plugin->IsAudioHost()
                            ? 2
                            : plugin->IsYouTubeUrl()
                            ? (streamIndex == 1 ? 1 : (streamIndex == 2 ? 2 : 0))
                            : (streamIndex == 0 ? 0 : 3);
                        const int codecIndex = AVS::ComboBox_GetCurrent(hCodec);
                        const std::vector<std::wstring>& codecs = plugin->AvailableVideoCodecs();
                        const std::wstring codec = codecIndex > 0 && (size_t)(codecIndex - 1) < codecs.size()
                            ? codecs[codecIndex - 1] : L"";
                        const int audioIndex = AVS::ComboBox_GetCurrent(hAudio);
                        const auto& tracks = plugin->m_audioTracks;
                        const std::wstring language = audioIndex > 0 && (size_t)(audioIndex - 1) < tracks.size()
                            ? tracks[audioIndex - 1].first : L"";
                        plugin->Start(url, quality, streamMode, codec, language);
                        EnableWindow(hEdit, FALSE);
                        EnableWindow(hSettings, FALSE);
                        SetWindowTextW(hAction, tr(L"Cancel").c_str());
                    }
                }
                break;
            }
            case 5: // About
            {
                NSUI::ShowAboutDialog(hwnd);
                break;
            }
            case 9: // Settings
            {
                if (IsWindowEnabled(hSettings))
                    NSUI::ShowSettingsDialog(hwnd, plugin);
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

        case WM_YT_NEXT_FORMAT_ATTEMPT:
            plugin->NextFormatAttempt();
            break;

        case WM_YT_RETRY_FORMATS:
            EnableWindow(hEdit, FALSE);
            EnableWindow(hAction, FALSE);
            EnableWindow(hSettings, FALSE);
            SetWindowTextW(hAction, tr(L"Check").c_str());
            plugin->RetryFormatsAfterLogin();
            break;

        case WM_YT_FAILED:
            EnableWindow(hEdit, TRUE);
            EnableWindow(hSettings, TRUE);
            SetWindowTextW(hAction, tr(L"Download").c_str());
            plugin->ReportFailure();
            break;

        case WM_YT_FORMATS_READY:
        {
            EnableWindow(hEdit, TRUE);
            EnableWindow(hAction, TRUE);
            EnableWindow(hSettings, TRUE);
            if (plugin->AvailableQualities().empty())
            {
                plugin->SetCaption(tr(L"Error"));
                plugin->ReportFailure();
                break;
            }

            std::vector<std::wstring> qualities{ tr(L"Best available") };
            for (int quality : plugin->AvailableQualities())
                qualities.push_back(std::to_wstring(quality) + L"p");
            AVS::ComboBox_SetItems(hQuality, qualities, 0);

            std::vector<std::wstring> streams;
            if (plugin->IsAudioHost())
            {
                streams.push_back(tr(L"Audio only"));
                EnableWindow(hQuality, FALSE);
                EnableWindow(hCodec, FALSE);
            }
            else
            {
                streams.push_back(tr(L"Video + audio"));
                if (plugin->IsYouTubeUrl())
                {
                    streams.push_back(tr(L"Video only"));
                    streams.push_back(tr(L"Audio only"));
                }
                // Twitch, Instagram and similar services often publish only an
                // already-muxed MP4. There is no genuine stream choice there, so
                // avoid presenting a misleading second option.
                else if (plugin->CanChooseStreamType())
                    streams.push_back(tr(L"Single stream"));
            }
            AVS::ComboBox_SetItems(hStream, streams, 0);

            // Twitch and some other services publish ready-made MP4 streams
            // but do not report codec metadata. "Any video codec" must still
            // remain selectable, just like the always-present stream choice.
            std::vector<std::wstring> codecs{ tr(L"Any video codec") };
            for (const std::wstring& codec : plugin->AvailableVideoCodecs())
                codecs.push_back(codec);
            AVS::ComboBox_SetItems(hCodec, codecs, 0);

            std::vector<std::wstring> tracks{ tr(L"Default audio track") };
            for (const auto& track : plugin->m_audioTracks)
                tracks.push_back(track.second);
            AVS::ComboBox_SetItems(hAudio, tracks, 0);
            EnableWindow(hAudio, TRUE);
            showFormatSelectors();
            SetWindowTextW(hAction, tr(L"Download").c_str());
            plugin->SetCaption(plugin->VideoTitle().empty()
                ? tr(L"Complete") : plugin->VideoTitle());
            break;
        }

        case WM_YT_COMPLETE:
            if (!plugin->m_path.empty() && NSSystemUtils::ExistsFile(plugin->m_path))
            {
                if (plugin->m_callback)
                    plugin->m_callback(PluginId(), export_str(plugin->m_path.c_str()), 0,
                                       plugin->m_callback_context);
            }
            DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            plugin->Stop();
            // Wake the local plugin message loop without posting WM_QUIT to
            // the host application's UI thread.
            PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
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
        wc.hbrBackground = WindowBackground();

        std::wstring iconPath = plugin->m_workDirectory + L"\\icon_internal.ico";

        HMODULE hModule = GetModuleHandle(NULL);
        wc.hIcon = (HICON)LoadImage(hModule, iconPath.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
        wc.hIconSm = (HICON)LoadImage(hModule, iconPath.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE | LR_DEFAULTCOLOR);

        RegisterClassEx(&wc);

        DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        std::wstring titleWindow = CTranslate::GetInstance().GetManager()->Translate(L"YouTube");

        RECT rc = { 0, 0, 600, 94 };
        AdjustWindowRectEx(&rc, dwStyle, FALSE, 0);

        HWND hostWindow = plugin->m_parentWindow;
        if (!hostWindow || !IsWindow(hostWindow))
        {
            hostWindow = GetActiveWindow();
            if (!hostWindow)
            {
                HWND foregroundWindow = GetForegroundWindow();
                DWORD processId = 0;

                if (foregroundWindow)
                    GetWindowThreadProcessId(foregroundWindow, &processId);

                if (processId == GetCurrentProcessId())
                    hostWindow = foregroundWindow;
            }
        }

        if (hostWindow)
            hostWindow = GetAncestor(hostWindow, GA_ROOT);

        plugin->SetHostWindow(hostWindow);

        const bool hostWasEnabled = hostWindow && IsWindowEnabled(hostWindow);

        HWND hwnd = CreateWindowExW(0, wc.lpszClassName, titleWindow.c_str(),
            dwStyle,
            CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
            hostWindow, nullptr, GetModuleHandleW(nullptr), plugin);

        if (hwnd)
        {
            if (hostWasEnabled)
                EnableWindow(hostWindow, FALSE);

            CenterWindow(hwnd);
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);

            MSG msg{};
            while (IsWindow(hwnd))
            {
                const BOOL messageResult = GetMessageW(&msg, nullptr, 0, 0);
                if (messageResult <= 0)
                    break;

                if (!IsDialogMessageW(hwnd, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
        }

        // Restore the host in one common exit path, including message-loop
        // errors. The host is never enabled if it was disabled beforehand.
        if (hostWasEnabled && hostWindow && IsWindow(hostWindow))
        {
            EnableWindow(hostWindow, TRUE);
            SetForegroundWindow(hostWindow);
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
        return TR(L"YouTube");
    }

    PLUGIN_API wchar_t* __stdcall PluginVersion()
    {
        return export_str(L"1.0.5");
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
            return NSYoutube::HasMediaToolkitLibraries();
        case AVS_AUDIO_EDITOR:
        case AVS_AUDIO_CONVERTER:
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

    PLUGIN_API void __stdcall SetParentWindow(PluginHandle handle, void* hwnd)
    {
        CYoutubePlugin* plugin = static_cast<CYoutubePlugin*>(handle);
        if (plugin)
            plugin->SetHostWindow(static_cast<HWND>(hwnd));
    }

    PLUGIN_API wchar_t* __stdcall GetMenuForContext(PluginHandle, Plugins::ContextType type)
    {
        nlohmann::json json = {};
        if (Plugins::ContextType::MediaLibrary == type) 
        {
            nlohmann::json obj;
            obj["text"] = NSStringUtils::wstring_to_utf8(CTranslate::GetInstance().GetManager()->Translate(L"YouTube"));
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


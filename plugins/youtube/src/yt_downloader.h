#ifndef YOUTUBE_DOWNLOADER_H
#define YOUTUBE_DOWNLOADER_H

// Builds the yt-dlp command line and runs it.
//
// Everything yt-dlp needs is shipped next to it inside the .avsp package:
// AVSMediaToolkit.exe for muxing and qjs.exe (QuickJS) for the JavaScript challenge
// YouTube puts in front of its stream URLs. Nothing installed on the user's
// machine is relied upon.

#include <string>
#include <sstream>
#include <cstring>
#include <windows.h>

#include "external_process_with_childs.h"
#include "browser_cookies.h"

namespace ytdl
{
    enum class Quality
    {
        BEST,
        WORST,
        P144,
        P240,
        P360,
        P480,
        P720,
        P1080,
        P1440,
        P2160
    };

    enum class Format
    {
        MP4,
        WEBM,
        MKV,
        AUDIO_ONLY
    };

    struct DownloadOptions
    {
        Quality quality = Quality::BEST;
        Format format = Format::MP4;

        std::wstring outputPath = L"./";
        std::wstring outputTemplate = L"%(title)s.%(ext)s";

        // When the UI has inspected a URL, it supplies its exact selector
        // here instead of one of the fixed Quality presets.
        std::wstring formatSelector;

        bool extractAudio = false;
    };

    class YouTubeDownloader : private NSProcesses::CProcessRunnerCallback
    {
    public:
        YouTubeDownloader(const std::wstring& dir, NSProcesses::CProcessRunnerCallback* callback)
            : m_callback(callback), m_manager(this)
        {
            m_dir = dir;
        }

        ~YouTubeDownloader()
        {
            stop();
        }

        void download(const std::wstring& url,
                      const DownloadOptions& options = DownloadOptions(),
                      const CookieStrategy& cookies = CookieStrategy())
        {
            m_options = options;
            m_url = url;
            m_cookies = cookies;
            m_videoId.clear(); m_audioId.clear(); m_finalPath.clear();
            m_videoExt.clear(); m_audioExt.clear(); m_toolkitError.clear();
            m_stage = Stage::Plan;
            m_manager.Start(buildDownloadCommand(url, options, cookies), {});
        }

        /** Ask yt-dlp for the formats it can see without downloading anything. */
        void listFormats(const std::wstring& url, const CookieStrategy& cookies = CookieStrategy())
        {
            m_stage = Stage::List;
            m_manager.Start(buildListFormatsCommand(url, cookies), {});
        }

        void stop()
        {
            m_manager.StopAll();
        }

    private:
        enum class Stage { List, Plan, Video, Audio, Merge };
        NSProcesses::CProcessRunnerCallback* m_callback;
        Stage m_stage = Stage::List;
        DownloadOptions m_options;
        CookieStrategy m_cookies;
        std::wstring m_url, m_videoId, m_audioId, m_videoExt, m_audioExt;
        std::wstring m_finalPath, m_videoPath, m_audioPath;
        std::string m_toolkitError;

        static std::wstring fromUtf8(const std::string& value)
        {
            int n = MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0);
            std::wstring result(n, L'\0');
            if (n) MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), &result[0], n);
            return result;
        }

        static bool nonemptyFile(const std::wstring& path)
        {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) &&
                !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                (data.nFileSizeHigh || data.nFileSizeLow);
        }

        void fail(const int id, const std::string& error)
        {
            m_callback->process_callback(id, NSProcesses::StreamType::StdErr, "ERROR: " + error);
            m_callback->process_callback(id, NSProcesses::StreamType::Stop, "1");
        }

        std::wstring trackCommand(const std::wstring& format, const std::wstring& path) const
        {
            std::wostringstream cmd;
            cmd << app() << L" --ignore-config --encoding utf-8 --no-playlist --fixup never "
                << L"--hls-prefer-native --downloader native --no-js-runtimes --js-runtimes "
                << quote(L"quickjs:" + m_dir + L"\\qjs.exe") << L" -f " << quote(format)
                << L" -o " << quote(path) << L" ";
            if (m_cookies.source == CookieSource::Browser)
                cmd << L"--cookies-from-browser " << quote(m_cookies.argument) << L" ";
            else if (m_cookies.source == CookieSource::File)
                cmd << L"--cookies " << quote(m_cookies.argument) << L" ";
            cmd << quote(m_url);
            return cmd.str();
        }

        void process_callback(const int& id, const NSProcesses::StreamType& type,
                              const std::string& message) override
        {
            using NSProcesses::StreamType;
            if (m_stage == Stage::Merge && type == StreamType::StdErr && !message.empty())
                m_toolkitError = message;
            if (m_stage == Stage::Plan && type == StreamType::StdOut)
            {
                struct Field { const char* marker; std::wstring* value; };
                Field fields[] = {{"__AVS_VIDEO__:", &m_videoId}, {"__AVS_AUDIO__:", &m_audioId},
                    {"__AVS_EXT__:", &m_videoExt}, {"__AVS_AEXT__:", &m_audioExt},
                    {"__AVS_OUTPUT__:", &m_finalPath}};
                for (const auto& field : fields)
                    if (message.compare(0, strlen(field.marker), field.marker) == 0)
                    {
                        *field.value = fromUtf8(message.substr(strlen(field.marker)));
                        return;
                    }
            }
            if (m_stage == Stage::List || (type != StreamType::Stop && type != StreamType::Terminate))
            {
                m_callback->process_callback(id, type, message);
                return;
            }
            if (type == StreamType::Terminate || message != "0")
            {
                const char* operation = m_stage == Stage::Merge ? "AVSMediaToolkit merge" : "yt-dlp";
                fail(id, std::string(operation) + " failed (" +
                    (type == StreamType::Terminate ? "Windows error " : "exit code ") + message + ")." +
                    (m_stage == Stage::Merge && !m_toolkitError.empty() ? " " + m_toolkitError : ""));
                return;
            }
            if (m_stage == Stage::Plan)
            {
                if (m_videoId.empty() || m_finalPath.empty())
                { fail(id, "yt-dlp did not return the selected track and output path."); return; }
                if (!m_audioId.empty())
                {
                    if (!((m_videoExt == L"mp4" && (m_audioExt == L"m4a" || m_audioExt == L"mp4")) ||
                          (m_videoExt == L"webm" && m_audioExt == L"webm")))
                    { fail(id, "Incompatible video and audio containers."); return; }
                    if (GetFileAttributesW((m_dir + L"\\AVSMediaToolkit.exe").c_str()) == INVALID_FILE_ATTRIBUTES)
                    { fail(id, "AVSMediaToolkit.exe is missing next to Youtube.dll."); return; }
                    const size_t dot = m_finalPath.find_last_of(L'.');
                    m_finalPath = m_finalPath.substr(0, dot) + L"." + m_videoExt;
                }
                // Intermediate names are rejected by the host's existing file scan.
                m_videoPath = m_audioId.empty() ? m_finalPath : m_options.outputPath + L"/video.f0." + m_videoExt;
                m_audioPath = m_options.outputPath + L"/audio.f1." + m_audioExt;
                m_stage = Stage::Video;
                m_manager.Start(trackCommand(m_videoId, m_videoPath), {});
                return;
            }
            if (m_stage == Stage::Video && !m_audioId.empty())
            {
                if (!nonemptyFile(m_videoPath)) { fail(id, "Downloaded video track is missing or empty."); return; }
                m_stage = Stage::Audio;
                m_manager.Start(trackCommand(m_audioId, m_audioPath), {});
                return;
            }
            if (m_stage == Stage::Audio)
            {
                if (!nonemptyFile(m_audioPath)) { fail(id, "Downloaded audio track is missing or empty."); return; }
                m_stage = Stage::Merge;
                m_manager.Start(quote(m_dir + L"\\AVSMediaToolkit.exe") + L" merge --container " +
                    (m_videoExt == L"mp4" ? L"quicktime" : L"matroska") +
                    L" --video " + quote(m_videoPath) + L" --audio " + quote(m_audioPath) +
                    L" --output " + quote(m_finalPath), {});
                return;
            }
            if (!nonemptyFile(m_finalPath)) { fail(id, "The output file is missing or empty."); return; }
            if (m_stage == Stage::Merge)
            { DeleteFileW(m_videoPath.c_str()); DeleteFileW(m_audioPath.c_str()); }
            m_callback->process_callback(id, StreamType::Stop, "0");
        }

        std::wstring                 m_dir;
        NSProcesses::CProcessManager m_manager;

        static std::wstring quote(const std::wstring& value)
        {
            return L"\"" + value + L"\"";
        }

        std::wstring app() const
        {
            return quote(m_dir + L"\\yt-dlp.exe");
        }

        static int qualityHeight(Quality quality)
        {
            switch (quality)
            {
            case Quality::P144:  return 144;
            case Quality::P240:  return 240;
            case Quality::P360:  return 360;
            case Quality::P480:  return 480;
            case Quality::P720:  return 720;
            case Quality::P1080: return 1080;
            case Quality::P1440: return 1440;
            case Quality::P2160: return 2160;
            default:             return 0;
            }
        }

        /**
         * Format selector that degrades instead of failing.
         *
         * YouTube offers separate mp4 video and m4a audio streams, so asking for
         * that pair directly gives the best result there. Most other services do
         * not: Twitch and many HLS sites only publish pre-muxed renditions, and
         * some publish formats with no height at all. Each "/" step below is a
         * looser fallback, ending in a bare "b" so an unusual site yields
         * something rather than "Requested format is not available".
         */
        static std::wstring buildFormatSelector(Quality quality)
        {
            if (quality == Quality::WORST)
                return L"wv[ext=mp4]+wa[ext=m4a]/wv[ext=mp4]+wa[ext=mp4]/wv[ext=webm]+wa[ext=webm][acodec=opus]/w";

            const int height = qualityHeight(quality);

            if (height <= 0)   // BEST
                return L"bv[ext=mp4]+ba[ext=m4a]/bv[ext=mp4]+ba[ext=mp4]/bv[ext=webm]+ba[ext=webm][acodec=opus]/b";

            const std::wstring cap = L"[height<=" + std::to_wstring(height) + L"]";

            return L"bv" + cap + L"[ext=mp4]+ba[ext=m4a]"   // YouTube and friends
                 + L"/bv" + cap + L"[ext=mp4]+ba[ext=mp4]"
                 + L"/bv" + cap + L"[ext=webm]+ba[ext=webm][acodec=opus]"                    // matching WebM tracks
                 + L"/b" + cap                               // pre-muxed rendition
                 + L"/b";                                    // no height info at all
        }

        std::wstring buildDownloadCommand(const std::wstring& url,
                                          const DownloadOptions& options,
                                          const CookieStrategy& cookies) const
        {
            std::wostringstream cmd;
            cmd << app() << L" --encoding utf-8 ";

            const std::wstring selector = options.extractAudio || options.format == Format::AUDIO_ONLY
                ? L"ba" : (options.formatSelector.empty()
                    ? buildFormatSelector(options.quality) : options.formatSelector);
            cmd << L"--ignore-config --simulate --fixup never -f " << quote(selector) << L" ";
            cmd << L"--print " << quote(L"__AVS_VIDEO__:%(requested_formats.0.format_id,format_id)s") << L" ";
            cmd << L"--print " << quote(L"__AVS_AUDIO__:%(requested_formats.1.format_id|)s") << L" ";
            cmd << L"--print " << quote(L"__AVS_EXT__:%(requested_formats.0.ext,ext)s") << L" ";
            cmd << L"--print " << quote(L"__AVS_AEXT__:%(requested_formats.1.ext|)s") << L" ";
            cmd << L"--print " << quote(L"__AVS_OUTPUT__:%(filename)s") << L" ";

            // Cookie source for this attempt. The caller walks the list built by
            // BuildCookieStrategies until one of them produces a file.
            switch (cookies.source)
            {
            case CookieSource::Browser:
                cmd << L"--cookies-from-browser " << quote(cookies.argument) << L" ";
                break;
            case CookieSource::File:
                cmd << L"--cookies " << quote(cookies.argument) << L" ";
                break;
            case CookieSource::None:
            default:
                break;
            }

            // A URL copied from a browser usually carries a list= parameter.
            // Without this the whole playlist lands in the temp directory and the
            // caller cannot tell which file it asked for.
            cmd << L"--no-playlist ";

            // QuickJS signs stream URLs; Toolkit handles the separate merge stage.
            cmd << L"--no-js-runtimes --js-runtimes "
                << quote(L"quickjs:" + m_dir + L"\\qjs.exe") << L" ";

            cmd << L"-o " << quote(options.outputPath + L"/" + options.outputTemplate) << L" ";
            cmd << quote(url);

            return cmd.str();
        }

        std::wstring buildListFormatsCommand(const std::wstring& url,
                                             const CookieStrategy& cookies) const
        {
            std::wostringstream cmd;
            cmd << app() << L" --ignore-config --encoding utf-8 --list-formats --no-playlist --print "
                << quote(L"__YT_TITLE__:%(title)s") << L" ";

            switch (cookies.source)
            {
            case CookieSource::Browser:
                cmd << L"--cookies-from-browser " << quote(cookies.argument) << L" ";
                break;
            case CookieSource::File:
                cmd << L"--cookies " << quote(cookies.argument) << L" ";
                break;
            case CookieSource::None:
            default:
                break;
            }

            cmd << L"--no-js-runtimes --js-runtimes "
                << quote(L"quickjs:" + m_dir + L"\\qjs.exe") << L" ";
            cmd << quote(url);
            return cmd.str();
        }
    };
}

#endif // YOUTUBE_DOWNLOADER_H

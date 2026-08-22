#ifndef YOUTUBE_DOWNLOADER_H
#define YOUTUBE_DOWNLOADER_H

// Builds the yt-dlp command line and runs it.
//
// Everything yt-dlp needs is shipped next to it inside the .avsp package:
// ffmpeg.exe for muxing and qjs.exe (QuickJS) for the JavaScript challenge
// YouTube puts in front of its stream URLs. Nothing installed on the user's
// machine is relied upon.

#include <string>
#include <sstream>
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

        bool extractAudio = false;
        std::wstring audioFormat = L"mp3";
    };

    class YouTubeDownloader
    {
    public:
        YouTubeDownloader(const std::wstring& dir, NSProcesses::CProcessRunnerCallback* callback)
            : m_manager(callback)
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
            m_manager.Start(buildDownloadCommand(url, options, cookies), {});
        }

        void stop()
        {
            m_manager.StopAll();
        }

    private:
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
                return L"wv*+wa/w";

            const int height = qualityHeight(quality);

            if (height <= 0)   // BEST
                return L"bv*[ext=mp4]+ba[ext=m4a]/bv*+ba/b";

            const std::wstring cap = L"[height<=" + std::to_wstring(height) + L"]";

            return L"bv*" + cap + L"[ext=mp4]+ba[ext=m4a]"   // YouTube and friends
                 + L"/bv*" + cap + L"+ba"                    // any split streams
                 + L"/b" + cap                               // pre-muxed rendition
                 + L"/b";                                    // no height info at all
        }

        std::wstring buildDownloadCommand(const std::wstring& url,
                                          const DownloadOptions& options,
                                          const CookieStrategy& cookies) const
        {
            std::wostringstream cmd;
            cmd << app() << L" ";

            if (options.extractAudio)
            {
                cmd << L"-x --audio-format " << options.audioFormat << L" ";
            }
            else
            {
                cmd << L"-f " << quote(buildFormatSelector(options.quality)) << L" ";

                switch (options.format)
                {
                case Format::MP4:
                    cmd << L"--merge-output-format mp4 ";
                    break;
                case Format::WEBM:
                    cmd << L"--merge-output-format webm ";
                    break;
                case Format::MKV:
                    cmd << L"--merge-output-format mkv ";
                    break;
                case Format::AUDIO_ONLY:
                    cmd << L"-f bestaudio --extract-audio --audio-format "
                        << options.audioFormat << L" ";
                    break;
                }
            }

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

            // Both tools travel inside the package next to yt-dlp.exe. ffmpeg
            // merges the separate video and audio streams; QuickJS executes the
            // JavaScript that YouTube's stream URLs are signed with, without
            // which extraction fails with "The page needs to be reloaded".
            cmd << L"--ffmpeg-location " << quote(m_dir) << L" ";
            cmd << L"--no-js-runtimes --js-runtimes "
                << quote(L"quickjs:" + m_dir + L"\\qjs.exe") << L" ";

            cmd << L"-o " << quote(options.outputPath + L"/" + options.outputTemplate) << L" ";
            cmd << quote(url);

            return cmd.str();
        }
    };
}

#endif // YOUTUBE_DOWNLOADER_H

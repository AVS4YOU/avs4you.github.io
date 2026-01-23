#ifndef YOUTUBE_DOWNLOADER_H
#define YOUTUBE_DOWNLOADER_H

#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <sstream>
#include <array>
#include <memory>
#include <thread>
#include <atomic>
#include <windows.h>

#include "external_process_with_childs.h"

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

    struct VideoInfo
    {
        std::string title;
        std::string duration;
        std::string uploader;
        std::string viewCount;
        std::string description;
    };

    class YouTubeDownloader
    {
    public:
        YouTubeDownloader(const std::wstring& dir, NSProcesses::CProcessRunnerCallback* callback) : m_manager(callback)
        {
            m_dir = dir;
        }

        ~YouTubeDownloader()
        {
            stop();
        }

        std::wstring getApp()
        {
            return L"\"" + m_dir + L"\\yt-dlp.exe\"";
        }

        void download(const std::wstring& url, const DownloadOptions& options = DownloadOptions())
        {
            std::wstring command = buildDownloadCommand(url, options);
            m_manager.Start(command, {});
        }

        void stop()
        {
            m_manager.StopAll();
        }

        VideoInfo getVideoInfo(const std::wstring& url)
        {
            std::wstring command = getApp() + L" --dump - json \"" + url + L"\"";
            std::string output = executeCommand(command);
            return parseVideoInfo(output);
        }

        std::vector<std::string> getAvailableFormats(const std::wstring& url)
        {
            std::wstring command = getApp() + L" -F \"" + url + L"\"";
            std::string output = executeCommand(command);
            return parseFormats(output);
        }

        bool isYtDlpInstalled()
        {
            std::wstring command = getApp() + L" --version > nul 2>&1";
            return _wsystem(command.c_str()) == 0;
        }

    private:
        std::wstring                 m_dir;
        NSProcesses::CProcessManager m_manager;

        std::wstring buildDownloadCommand(const std::wstring& url, const DownloadOptions& options)
        {
            std::wostringstream cmd;
            cmd << getApp() << L" ";

            if (options.extractAudio)
            {
                cmd << L"-x --audio-format " << options.audioFormat << " ";
            }
            else
            {
                switch (options.quality)
                {
                case Quality::BEST:
                    cmd << L"-f \"bestvideo[ext=mp4]+bestaudio[ext=m4a]/best\" ";
                    break;
                case Quality::WORST:
                    cmd << L"-f worst ";
                    break;
                case Quality::P144:
                    cmd << L"-f \"bestvideo[height<=144][ext=mp4]+bestaudio[ext=m4a]/best[height<=144]\" ";
                    break;
                case Quality::P240:
                    cmd << L"-f \"bestvideo[height<=240][ext=mp4]+bestaudio[ext=m4a]/best[height<=240]\" ";
                    break;
                case Quality::P360:
                    cmd << L"-f \"bestvideo[height<=360][ext=mp4]+bestaudio[ext=m4a]/best[height<=360]\" ";
                    break;
                case Quality::P480:
                    cmd << L"-f \"bestvideo[height<=480][ext=mp4]+bestaudio[ext=m4a]/best[height<=480]\" ";
                    break;
                case Quality::P720:
                    cmd << L"-f \"bestvideo[height<=720][ext=mp4]+bestaudio[ext=m4a]/best[height<=720]\" ";
                    break;
                case Quality::P1080:
                    cmd << L"-f \"bestvideo[height<=1080][ext=mp4]+bestaudio[ext=m4a]/best[height<=1080]\" ";
                    break;
                case Quality::P1440:
                    cmd << L"-f \"bestvideo[height<=1440][ext=mp4]+bestaudio[ext=m4a]/best[height<=1440]\" ";
                    break;
                case Quality::P2160:
                    cmd << L"-f \"bestvideo[height<=2160][ext=mp4]+bestaudio[ext=m4a]/best[height<=2160]\" ";
                    break;
                }

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
                    cmd << L"-f bestaudio --extract-audio --audio-format " << options.audioFormat << " ";
                    break;
                }
            }
            
            cmd << L"--cookies-from-browser firefox ";

            if (false) 
            {
                cmd << L"--user-agent \"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\" ";
            }

            if (false) 
            {
                cmd << L"--force-ipv6 ";
            }

            if (false) 
            {
                cmd << L"--sleep-interval " << 5 << " ";
            }
            
            cmd << L"-o \"" << options.outputPath << L"/" << options.outputTemplate << L"\" ";

            cmd << L"\"" << url << L"\"";

            return cmd.str();
        }

        std::string executeCommand(const std::wstring& command)
        {
            std::array<char, 128> buffer;
            std::string result;

            FILE* pipe = _wpopen(command.c_str(), L"r");
            if (!pipe) {
                throw std::runtime_error("Failed to execute command.");
            }

            while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                result += buffer.data();
            }

            _pclose(pipe);
            return result;
        }

        VideoInfo parseVideoInfo(const std::string& jsonOutput)
        {
            VideoInfo info;
            info.title = extractJsonField(jsonOutput, "title");
            info.duration = extractJsonField(jsonOutput, "duration");
            info.uploader = extractJsonField(jsonOutput, "uploader");
            info.viewCount = extractJsonField(jsonOutput, "view_count");
            info.description = extractJsonField(jsonOutput, "description");
            return info;
        }

        std::string extractJsonField(const std::string& json, const std::string& field)
        {
            std::string search = "\"" + field + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return "";

            pos += search.length();
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '"')) pos++;

            size_t endPos = pos;
            bool inString = json[pos - 1] == '"';

            if (inString)
            {
                endPos = json.find('"', pos);
            }
            else
            {
                while (endPos < json.length() && json[endPos] != ',' && json[endPos] != '}')
                {
                    endPos++;
                }
            }

            return json.substr(pos, endPos - pos);
        }

        std::vector<std::string> parseFormats(const std::string& output)
        {
            std::vector<std::string> formats;
            std::istringstream stream(output);
            std::string line;

            while (std::getline(stream, line))
            {
                if (!line.empty() && line.find("format code") == std::string::npos)
                {
                    formats.push_back(line);
                }
            }

            return formats;
        }
    };
}

#endif // YOUTUBE_DOWNLOADER_H
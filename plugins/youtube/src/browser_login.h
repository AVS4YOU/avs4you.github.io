#ifndef YOUTUBE_BROWSER_LOGIN_H
#define YOUTUBE_BROWSER_LOGIN_H

// Signing in from inside the plugin.
//
// Reading cookies out of the user's own browser profile does not work for the
// browsers most people actually use: a running Chrome or Edge holds its cookie
// database open, and its cookies are encrypted with App-Bound Encryption
// (os_crypt.app_bound_encrypted_key in Local State, "v20" values), which yt-dlp
// cannot decrypt. Telling somebody to close Chrome is not a fix.
//
// So the plugin keeps a browser profile of its own. It launches the user's real
// browser against that profile, the user signs in once, closes the window, and
// yt-dlp reads the cookies from there afterwards.
//
// Nothing about this is YouTube specific. The profile is an ordinary browser
// profile, so signing in to Twitch, Instagram, X, VK or anything else yt-dlp
// supports puts those cookies in the same place - which is why the sign-in
// window opens at the site the user is actually downloading from.
//
// Three things make this work, all verified against Chrome 140 and
// yt-dlp 2026.08.19:
//
//   * A profile created through --user-data-dir gets no app_bound_encrypted_key
//     at all - only the plain DPAPI "encrypted_key" - so its cookies are "v10"
//     and yt-dlp decrypts them. This holds for a headful browser, not just a
//     headless one.
//   * yt-dlp accepts an absolute profile path
//     (--cookies-from-browser "chrome:<dir>") and finds both the cookie
//     database and the key inside it.
//   * The profile is separate from the one the user browses with, so it is not
//     held open, and the user's own session is never touched.
//
// It is also a real browser rather than an embedded web view, so Google does not
// refuse the sign-in with "this browser or app may not be secure".

#include <string>

#include <windows.h>

namespace ytdl
{
    /** Browser used to sign in. Chromium family only - the profile layout matters. */
    struct LoginBrowser
    {
        std::wstring executable;
        std::wstring label;

        bool Valid() const { return !executable.empty(); }
    };

    namespace detail
    {
        inline std::wstring RegistryString(HKEY root, const wchar_t* subKey, const wchar_t* value, DWORD flags)
        {
            HKEY key = NULL;
            if (::RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE | flags, &key) != ERROR_SUCCESS)
                return std::wstring();

            wchar_t buffer[MAX_PATH * 2] = { 0 };
            DWORD size = sizeof(buffer);
            DWORD type = 0;

            std::wstring result;

            if (::RegQueryValueExW(key, value, NULL, &type, (LPBYTE)buffer, &size) == ERROR_SUCCESS &&
                (type == REG_SZ || type == REG_EXPAND_SZ))
            {
                buffer[(sizeof(buffer) / sizeof(buffer[0])) - 1] = L'\0';
                result.assign(buffer);
            }

            ::RegCloseKey(key);
            return result;
        }

        /** Pulls "C:\...\chrome.exe" out of a shell open command. */
        inline std::wstring ExecutableFromCommand(const std::wstring& command)
        {
            if (command.empty())
                return std::wstring();

            if (command[0] == L'"')
            {
                const size_t end = command.find(L'"', 1);
                if (end == std::wstring::npos)
                    return std::wstring();

                return command.substr(1, end - 1);
            }

            const size_t space = command.find(L' ');
            return (space == std::wstring::npos) ? command : command.substr(0, space);
        }

        inline bool IsFile(const std::wstring& path)
        {
            if (path.empty())
                return false;

            const DWORD attributes = ::GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES &&
                   (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
        }

        inline std::wstring FileName(const std::wstring& path)
        {
            const size_t slash = path.find_last_of(L"\\/");
            return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
        }

        inline bool EqualsNoCase(const std::wstring& a, const wchar_t* b)
        {
            return _wcsicmp(a.c_str(), b) == 0;
        }

        /** App Paths lookup. HKLM before HKCU: HKCU often points at Chrome Canary. */
        inline std::wstring AppPath(const wchar_t* executable)
        {
            const std::wstring subKey =
                std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\") + executable;

            const std::wstring candidates[] =
            {
                RegistryString(HKEY_LOCAL_MACHINE, subKey.c_str(), NULL, KEY_WOW64_64KEY),
                RegistryString(HKEY_LOCAL_MACHINE, subKey.c_str(), NULL, KEY_WOW64_32KEY),
                RegistryString(HKEY_CURRENT_USER,  subKey.c_str(), NULL, 0)
            };

            for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
            {
                if (IsFile(candidates[i]))
                    return candidates[i];
            }

            return std::wstring();
        }
    }

    /** Chromium executables worth signing in with, best first. */
    inline const wchar_t* const* LoginExecutables(size_t& count)
    {
        static const wchar_t* const executables[] =
        {
            L"chrome.exe",     // most common
            L"msedge.exe",     // always present on Windows 10 and 11
            L"brave.exe",
            L"vivaldi.exe",
            L"opera.exe",
            L"chromium.exe"
        };

        count = sizeof(executables) / sizeof(executables[0]);
        return executables;
    }

    inline std::wstring LoginBrowserLabel(const std::wstring& executable)
    {
        const std::wstring name = detail::FileName(executable);

        if (detail::EqualsNoCase(name, L"chrome.exe"))   return L"Chrome";
        if (detail::EqualsNoCase(name, L"msedge.exe"))   return L"Edge";
        if (detail::EqualsNoCase(name, L"brave.exe"))    return L"Brave";
        if (detail::EqualsNoCase(name, L"vivaldi.exe"))  return L"Vivaldi";
        if (detail::EqualsNoCase(name, L"opera.exe"))    return L"Opera";
        if (detail::EqualsNoCase(name, L"chromium.exe")) return L"Chromium";

        return name;
    }

    /**
     * Which browser to sign in with: the user's default browser when it is a
     * Chromium one, otherwise the first one installed. Edge ships with Windows,
     * so this practically always finds something.
     */
    inline LoginBrowser FindLoginBrowser()
    {
        LoginBrowser result;

        size_t count = 0;
        const wchar_t* const* executables = LoginExecutables(count);

        // The default browser is the one the user is already signed in with, so
        // its saved passwords and autofill make the sign-in easiest.
        const std::wstring progId = detail::RegistryString(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\https\\UserChoice",
            L"ProgID", 0);

        if (!progId.empty())
        {
            const std::wstring command = detail::RegistryString(
                HKEY_CLASSES_ROOT, (progId + L"\\shell\\open\\command").c_str(), NULL, 0);

            const std::wstring executable = detail::ExecutableFromCommand(command);
            const std::wstring name = detail::FileName(executable);

            for (size_t i = 0; i < count; ++i)
            {
                if (detail::EqualsNoCase(name, executables[i]) && detail::IsFile(executable))
                {
                    result.executable = executable;
                    result.label = LoginBrowserLabel(executable);
                    return result;
                }
            }
        }

        for (size_t i = 0; i < count; ++i)
        {
            const std::wstring executable = detail::AppPath(executables[i]);

            if (!executable.empty())
            {
                result.executable = executable;
                result.label = LoginBrowserLabel(executable);
                return result;
            }
        }

        return result;
    }

    /** The plugin's own browser profile. */
    inline std::wstring LoginProfileDirectory(const std::wstring& workDirectory)
    {
        return workDirectory + L"\\signin-profile";
    }

    /**
     * Where to send the sign-in window, given the URL the user wants to
     * download. Signing in has to happen on the site the download comes from,
     * so this follows the pasted link rather than assuming YouTube.
     */
    /**
     * Host of an http(s) link, or empty when the box holds nothing usable.
     * Restricting to http(s) means a pasted file: or javascript: URL cannot
     * steer the browser somewhere unintended.
     */
    inline std::wstring TargetHost(const std::wstring& target)
    {
        const size_t schemeEnd = target.find(L"://");
        if (schemeEnd == std::wstring::npos)
            return std::wstring();

        const std::wstring scheme = target.substr(0, schemeEnd);
        if (_wcsicmp(scheme.c_str(), L"http") != 0 && _wcsicmp(scheme.c_str(), L"https") != 0)
            return std::wstring();

        const size_t hostStart = schemeEnd + 3;
        size_t hostEnd = target.find_first_of(L"/?#", hostStart);
        if (hostEnd == std::wstring::npos)
            hostEnd = target.size();

        const std::wstring host = target.substr(hostStart, hostEnd - hostStart);
        if (host.empty() || host.find(L'.') == std::wstring::npos)
            return std::wstring();

        return host;
    }

    inline bool IsYouTubeHost(const std::wstring& host)
    {
        return host.find(L"youtube.com") != std::wstring::npos ||
               host.find(L"youtu.be") != std::wstring::npos;
    }

    /** Sign-in page for a host. Empty host means the caller should offer the list. */
    inline std::wstring SignInUrl(const std::wstring& host)
    {
        if (host.empty() || IsYouTubeHost(host))
        {
            // Google's sign-in page rather than the YouTube home page: it is the
            // one that actually asks for the account.
            return L"https://accounts.google.com/ServiceLogin"
                   L"?service=youtube&continue=https%3A%2F%2Fwww.youtube.com%2F";
        }

        return L"https://" + host + L"/";
    }

    /**
     * Name to show the user for a host. Falls back to the bare host, so an
     * unlisted site still reads sensibly ("Sign in to rumble.com").
     */
    inline std::wstring ServiceName(const std::wstring& host)
    {
        struct Known { const wchar_t* match; const wchar_t* name; };

        static const Known known[] =
        {
            { L"youtube.com",   L"YouTube"     },
            { L"youtu.be",      L"YouTube"     },
            { L"twitch.tv",     L"Twitch"      },
            { L"vimeo.com",     L"Vimeo"       },
            { L"dailymotion",   L"Dailymotion" },
            { L"tiktok.com",    L"TikTok"      },
            { L"instagram.com", L"Instagram"   },
            { L"facebook.com",  L"Facebook"    },
            { L"twitter.com",   L"X / Twitter" },
            { L"x.com",         L"X / Twitter" },
            { L"reddit.com",    L"Reddit"      },
            { L"vk.com",        L"VK"          },
            { L"rutube.ru",     L"Rutube"      },
            { L"soundcloud",    L"SoundCloud"  },
            { L"nicovideo",     L"Niconico"    },
            { L"bilibili",      L"Bilibili"    },
            { L"rumble.com",    L"Rumble"      },
            { L"kick.com",      L"Kick"        }
        };

        for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
        {
            if (host.find(known[i].match) != std::wstring::npos)
                return known[i].name;
        }

        std::wstring name = host;
        if (name.compare(0, 4, L"www.") == 0)
            name.erase(0, 4);

        return name;
    }

    /** True once a sign-in has actually stored cookies in that profile. */
    inline bool LoginProfileHasCookies(const std::wstring& workDirectory)
    {
        const std::wstring profile = LoginProfileDirectory(workDirectory);

        const wchar_t* relative[] =
        {
            L"Default\\Network\\Cookies",
            L"Default\\Cookies"
        };

        for (size_t i = 0; i < sizeof(relative) / sizeof(relative[0]); ++i)
        {
            const std::wstring path = profile + L"\\" + relative[i];

            WIN32_FILE_ATTRIBUTE_DATA data;
            if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
                continue;

            // An untouched database is a few KB of empty SQLite pages.
            if (data.nFileSizeHigh > 0 || data.nFileSizeLow > 8192)
                return true;
        }

        return false;
    }

    /**
     * Opens signInUrl in the plugin's own profile so the user can sign in.
     * Returns the browser process handle - the caller closes it. NULL on failure.
     */
    inline HANDLE StartLogin(const LoginBrowser& browser, const std::wstring& workDirectory,
                             const std::wstring& signInUrl)
    {
        if (!browser.Valid())
            return NULL;

        const std::wstring profile = LoginProfileDirectory(workDirectory);
        ::CreateDirectoryW(profile.c_str(), NULL);

        // --no-first-run and --no-default-browser-check keep the fresh profile
        // from opening welcome tabs over the sign-in page.
        std::wstring command = L"\"" + browser.executable + L"\"";
        command += L" --user-data-dir=\"" + profile + L"\"";
        command += L" --no-first-run --no-default-browser-check --no-service-autorun";
        command += L" \"" + signInUrl + L"\"";

        STARTUPINFOW startup;
        memset(&startup, 0, sizeof(startup));
        startup.cb = sizeof(startup);

        PROCESS_INFORMATION process;
        memset(&process, 0, sizeof(process));

        // CreateProcessW may write to the command line buffer.
        std::wstring mutableCommand = command;

        if (!::CreateProcessW(browser.executable.c_str(), &mutableCommand[0], NULL, NULL, FALSE,
                              0, NULL, NULL, &startup, &process))
        {
            return NULL;
        }

        if (process.hThread)
            ::CloseHandle(process.hThread);

        return process.hProcess;
    }

}

#endif // YOUTUBE_BROWSER_LOGIN_H

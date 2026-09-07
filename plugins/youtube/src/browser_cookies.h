#ifndef YOUTUBE_BROWSER_COOKIES_H
#define YOUTUBE_BROWSER_COOKIES_H

// Where cookies come from, and in what order they are tried.
//
// There used to be a scan over every installed browser here. It was dropped:
// for the browsers most people actually use it cannot work at all. Chrome and
// Edge encrypt their cookie store with App-Bound Encryption
// (os_crypt.app_bound_encrypted_key in Local State, "v20" values) which yt-dlp
// cannot decrypt, and they hold the database open while running so it cannot
// even be copied. Walking eight browsers therefore mostly bought a ten second
// stall, a confusing "close your browser" message, and a reason to read cookie
// stores that are none of the plugin's business.
//
// What replaced it is browser_login.h: the plugin owns one browser profile, the
// user signs in there once, and those cookies work for every service. So there
// are only three sources left, tried in this order:
//
//   1. no cookies at all - most videos need none, it is the fastest path, and
//      it keeps the user's accounts out of the request entirely
//   2. the plugin's own sign-in profile
//   3. a cookies.txt the user exported by hand - the escape hatch for anything
//      the sign-in window cannot cover

#include <string>
#include <vector>

#include <windows.h>

#include "browser_login.h"

namespace ytdl
{
    enum class CookieSource
    {
        None,       // no cookie options at all
        File,       // --cookies <path to a Netscape cookies.txt>
        Browser     // --cookies-from-browser <spec>
    };

    struct CookieStrategy
    {
        CookieSource source;
        std::wstring argument;   // browser spec, or the path to cookies.txt

        CookieStrategy()
            : source(CookieSource::None)
        {
        }

        CookieStrategy(CookieSource s, const std::wstring& a)
            : source(s), argument(a)
        {
        }
    };

    namespace detail
    {
        inline bool FileExists(const std::wstring& path)
        {
            if (path.empty())
                return false;

            const DWORD attributes = ::GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES &&
                   (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
        }
    }

    /** Every cookie source worth trying, best first. */
    inline std::vector<CookieStrategy> BuildCookieStrategies(const std::wstring& workDirectory)
    {
        std::vector<CookieStrategy> result;

        result.push_back(CookieStrategy(CookieSource::None, L""));

        if (LoginProfileHasCookies(workDirectory))
        {
            // "chrome" regardless of which Chromium browser created the profile:
            // given an explicit path, yt-dlp takes the decryption key from the
            // Local State file inside that directory.
            result.push_back(CookieStrategy(CookieSource::Browser,
                                            L"chrome:" + LoginProfileDirectory(workDirectory)));
        }

        const std::wstring cookieFile = workDirectory + L"\\cookies.txt";
        if (detail::FileExists(cookieFile))
            result.push_back(CookieStrategy(CookieSource::File, cookieFile));

        return result;
    }
}

#endif // YOUTUBE_BROWSER_COOKIES_H

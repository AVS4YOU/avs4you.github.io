#ifndef YOUTUBE_FAILURE_REASON_H
#define YOUTUBE_FAILURE_REASON_H

// Classifying a failed download from what yt-dlp printed.
//
// This decides what the user is shown, so it is worth keeping honest and
// testable on its own. Two questions, deliberately answered by two functions
// with different thresholds - see the comments below.

#include <string>
#include <cctype>
#include <cstring>

namespace NSYoutube
{
    inline bool Mentions(const std::string& haystack, const char* needle)
    {
        const size_t length = strlen(needle);
        if (length == 0 || haystack.size() < length)
            return false;

        for (size_t i = 0; i + length <= haystack.size(); ++i)
        {
            size_t j = 0;
            while (j < length && tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j]))
                ++j;

            if (j == length)
                return true;
        }

        return false;
    }

    /**
     * Does the failure explicitly say an account is missing?
     *
     * This one has to be strict, because it decides whether the user is told
     * "sign in to X". Guessing wrong means claiming a deleted or geo-blocked
     * video needs a login, which sends people off signing in for nothing.
     */
    inline bool LooksLikeSignInRequired(const std::string& log)
    {
        static const char* markers[] =
        {
            "sign in", "not a bot", "private video", "members-only", "member's-only",
            "age-restricted", "confirm your age", "login required", "log in",
            "subscriber", "http error 401", "http error 403",
            "this video is available to", "requires authentication"
        };

        for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i)
        {
            if (Mentions(log, markers[i]))
                return true;
        }

        return false;
    }

    /**
     * Did the download fail because yt-dlp had no JavaScript runtime?
     *
     * YouTube protects its stream URLs with an "n challenge" that has to be
     * solved by running its JavaScript. yt-dlp ships the solver script
     * (yt_dlp_ejs) inside the executable but not a runtime to execute it, and
     * without one the player response is rejected. That surfaces as the
     * distinctly unhelpful "The page needs to be reloaded".
     *
     * This is not an account problem, and it must not be reported as one: the
     * user can be perfectly signed in and still hit it.
     */
    inline bool LooksLikeMissingJsRuntime(const std::string& log)
    {
        static const char* markers[] =
        {
            "n challenge solving failed",
            "javascript runtime",
            "needs to be reloaded"
        };

        for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i)
        {
            if (Mentions(log, markers[i]))
                return true;
        }

        return false;
    }

    /**
     * Should the download be retried with a different cookie source?
     *
     * Deliberately more permissive than LooksLikeSignInRequired: another attempt
     * only costs a couple of seconds, while a missed retry costs the download.
     * Only failures no cookie can fix are ruled out.
     */
    inline bool WorthAnotherCookieSource(const std::string& log)
    {
        if (LooksLikeSignInRequired(log))
            return true;

        // A running browser holds its cookie database open, so this attempt
        // never even reached the site - the next source is worth trying.
        if (Mentions(log, "could not copy") || Mentions(log, "cookies"))
            return true;

        static const char* hopeless[] =
        {
            "is not a valid url", "unsupported url", "video unavailable",
            "requested format is not available", "no video formats found",
            "no space left", "removed by the uploader", "copyright"
        };

        for (size_t i = 0; i < sizeof(hopeless) / sizeof(hopeless[0]); ++i)
        {
            if (Mentions(log, hopeless[i]))
                return false;
        }

        return true;
    }
}

#endif // YOUTUBE_FAILURE_REASON_H

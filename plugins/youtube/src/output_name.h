#ifndef YOUTUBE_OUTPUT_NAME_H
#define YOUTUBE_OUTPUT_NAME_H

// Turning a video title into a usable file name.
//
// yt-dlp names the file after the title and only strips what the filesystem
// forbids, so what lands on disk keeps everything the uploader typed: trailing
// hashtag clouds, line breaks, runs of spaces. The result is technically valid
// and horrible to look at.
//
// CleanFileStem cuts that back to a plain name, and never returns an empty one -
// a title made of nothing but hashtags is common enough ("#shorts") that the
// caller always needs a fallback.

#include <string>

namespace NSYoutube
{
    /** Longest stem we keep, so the whole path stays comfortably under MAX_PATH. */
    const size_t MAX_STEM_LENGTH = 120;

    inline bool IsSpace(wchar_t c)
    {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\f' || c == L'\v';
    }

    /**
     * Characters that have no business in a file name.
     *
     * The first row is what Windows rejects. The second is what yt-dlp puts
     * there instead: rather than dropping them it substitutes lookalikes from
     * other Unicode blocks, so a title with a slash arrives as "SHOW: A/B" ->
     * "SHOW: A⧸B". Valid, and it reads like a mistake, so treat the substitutes
     * exactly like the characters they stand in for.
     */
    inline bool IsForbidden(wchar_t c)
    {
        switch (c)
        {
        case L'\\': case L'/': case L':': case L'*':
        case L'?':  case L'"': case L'<': case L'>': case L'|':
            return true;

        case 0x29F8: case 0x29F9:                          // big solidus, reverse solidus
        case 0xFF1A: case 0xFF0A: case 0xFF1F:             // fullwidth : * ?
        case 0xFF02: case 0xFF1C: case 0xFF1E: case 0xFF5C: // fullwidth " < > |
            return true;

        default:
            return false;
        }
    }

    /**
     * Cleans a title into a file name stem.
     *
     *   - hashtag words are dropped, but only where a hashtag can start: at the
     *     beginning or after whitespace. That keeps "C#", "F#" and "Do#" intact.
     *   - line breaks, tabs and control characters become spaces
     *   - characters the filesystem rejects are dropped
     *   - runs of whitespace collapse to one space
     *   - leading and trailing whitespace, dots, dashes and underscores go: a
     *     trailing dot or space makes a path Windows cannot open
     *   - the result is truncated to MAX_STEM_LENGTH
     *
     * Returns fallback when nothing usable is left.
     */
    inline std::wstring CleanFileStem(const std::wstring& title, const std::wstring& fallback)
    {
        std::wstring out;
        out.reserve(title.size());

        bool atWordStart = true;   // start of string, or just after whitespace
        bool skipping = false;     // inside a hashtag word being dropped

        for (size_t i = 0; i < title.size(); ++i)
        {
            const wchar_t c = title[i];
            const bool space = IsSpace(c) || (c > 0 && c < 0x20);

            if (space)
            {
                skipping = false;
                atWordStart = true;

                if (!out.empty() && out[out.size() - 1] != L' ')
                    out.push_back(L' ');

                continue;
            }

            if (c == L'#' && atWordStart)
                skipping = true;

            atWordStart = false;

            if (skipping || IsForbidden(c))
                continue;

            out.push_back(c);
        }

        // Trim both ends of anything that reads as padding, plus the trailing
        // dots and spaces Windows refuses in a path.
        const wchar_t* trim = L" .-_";
        const size_t first = out.find_first_not_of(trim);
        if (first == std::wstring::npos)
            return fallback;

        const size_t last = out.find_last_not_of(trim);
        out = out.substr(first, last - first + 1);

        if (out.size() > MAX_STEM_LENGTH)
        {
            out.resize(MAX_STEM_LENGTH);

            // Prefer cutting at a word boundary, but only if that keeps most of
            // the name; otherwise a long unbroken title would shrink to nothing.
            const size_t space = out.find_last_of(L' ');
            if (space != std::wstring::npos && space > MAX_STEM_LENGTH / 2)
                out.resize(space);

            const size_t end = out.find_last_not_of(trim);
            if (end == std::wstring::npos)
                return fallback;

            out = out.substr(0, end + 1);
        }

        return out.empty() ? fallback : out;
    }

    /**
     * Something identifying out of the URL, for when the title cleans away to
     * nothing: the "v" parameter if there is one, otherwise the last path
     * segment. Empty when the URL yields nothing usable.
     */
    inline std::wstring VideoIdFromUrl(const std::wstring& url)
    {
        const size_t query = url.find(L"v=");
        if (query != std::wstring::npos)
        {
            const size_t start = query + 2;
            size_t end = url.find_first_of(L"&#", start);
            if (end == std::wstring::npos)
                end = url.size();

            const std::wstring id = url.substr(start, end - start);
            if (!id.empty())
                return CleanFileStem(id, L"");
        }

        std::wstring path = url;

        const size_t cut = path.find_first_of(L"?#");
        if (cut != std::wstring::npos)
            path.erase(cut);

        while (!path.empty() && (path[path.size() - 1] == L'/' || path[path.size() - 1] == L'\\'))
            path.erase(path.size() - 1);

        const size_t slash = path.find_last_of(L"/\\");
        const std::wstring segment = (slash == std::wstring::npos) ? path : path.substr(slash + 1);

        return CleanFileStem(segment, L"");
    }
}

#endif // YOUTUBE_OUTPUT_NAME_H

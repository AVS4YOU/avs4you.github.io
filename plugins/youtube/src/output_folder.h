#ifndef YOUTUBE_OUTPUT_FOLDER_H
#define YOUTUBE_OUTPUT_FOLDER_H

// Where finished downloads go, and signing out.
//
// Downloads land in the user's Videos\YouTube unless another folder was chosen
// in Settings. The choice is one line in output-folder.txt in the plugin's work
// directory (%LOCALAPPDATA%\avs_plugin_youtube) - not next to the DLL, which the
// host replaces when the plugin is updated.
//
// A finished file is moved there from the work directory's temp folder. The
// move may cross drives, and it never replaces a file that is already there:
// a taken name gets " (2)", " (3)" and so on.
//
// Signing out removes everything the downloads could take an account from: the
// plugin's own browser profile (see browser_login.h) and a cookies.txt the user
// put there by hand (see browser_cookies.h).

#include <string>

#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

#include "browser_login.h"

namespace ytdl
{
    inline std::wstring OutputFolderFile(const std::wstring& workDirectory)
    {
        return workDirectory + L"\\output-folder.txt";
    }

    /**
     * Videos\YouTube. Videos as the shell knows it, so a Videos folder the user
     * moved to another drive is followed. The work directory only if Windows
     * has no Videos folder at all.
     */
    inline std::wstring DefaultOutputFolder(const std::wstring& workDirectory)
    {
        std::wstring videos;

        PWSTR known = NULL;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Videos, 0, NULL, &known)) && known)
            videos = known;
        ::CoTaskMemFree(known);

        if (videos.empty())
        {
            wchar_t buffer[MAX_PATH] = { 0 };
            if (SUCCEEDED(::SHGetFolderPathW(NULL, CSIDL_MYVIDEO, NULL, 0, buffer)))
                videos = buffer;
        }

        return videos.empty() ? workDirectory : videos + L"\\YouTube";
    }

    namespace detail
    {
        /** Without surrounding blanks and quotes, and without a trailing backslash unless it is a root. */
        inline std::wstring TidyFolder(std::wstring folder)
        {
            const wchar_t* blanks = L" \t\r\n\"";
            const size_t first = folder.find_first_not_of(blanks);
            if (first == std::wstring::npos)
                return std::wstring();

            folder = folder.substr(first, folder.find_last_not_of(blanks) - first + 1);

            while (folder.size() > 3 && (folder.back() == L'\\' || folder.back() == L'/'))
                folder.pop_back();

            return folder;
        }
    }

    /**
     * The folder chosen in Settings, or the default when nothing was chosen or
     * the file holds nothing usable. Environment variables are expanded, so the
     * file may also be written by hand ("%USERPROFILE%\Downloads").
     */
    inline std::wstring LoadOutputFolder(const std::wstring& workDirectory)
    {
        std::wstring folder;

        HANDLE file = ::CreateFileW(OutputFolderFile(workDirectory).c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE)
        {
            char buffer[8192];
            DWORD read = 0;
            if (::ReadFile(file, buffer, sizeof(buffer) - 1, &read, NULL) && read > 0)
            {
                const char* text = buffer;
                int length = (int)read;
                if (length >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
                    (unsigned char)text[2] == 0xBF)
                {
                    text += 3;
                    length -= 3;
                }

                const int wide = ::MultiByteToWideChar(CP_UTF8, 0, text, length, NULL, 0);
                if (wide > 0)
                {
                    std::wstring content(wide, L'\0');
                    ::MultiByteToWideChar(CP_UTF8, 0, text, length, &content[0], wide);
                    folder = detail::TidyFolder(content.substr(0, content.find_first_of(L"\r\n")));
                }
            }
            ::CloseHandle(file);
        }

        if (!folder.empty())
        {
            wchar_t expanded[MAX_PATH * 2] = { 0 };
            const DWORD size = ::ExpandEnvironmentStringsW(folder.c_str(), expanded,
                                                           sizeof(expanded) / sizeof(expanded[0]));
            if (size > 0 && size <= sizeof(expanded) / sizeof(expanded[0]))
                folder = detail::TidyFolder(expanded);
        }

        return folder.empty() ? DefaultOutputFolder(workDirectory) : folder;
    }

    /** Writes the choice as one UTF-8 line. False when the file could not be written. */
    inline bool SaveOutputFolder(const std::wstring& workDirectory, const std::wstring& folder)
    {
        const std::wstring line = detail::TidyFolder(folder) + L"\r\n";
        const int length = ::WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), NULL, 0, NULL, NULL);
        if (length <= 0)
            return false;

        std::string utf8 = "\xEF\xBB\xBF";
        const size_t bom = utf8.size();
        utf8.resize(bom + length);
        ::WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), &utf8[bom], length, NULL, NULL);

        HANDLE file = ::CreateFileW(OutputFolderFile(workDirectory).c_str(), GENERIC_WRITE, 0, NULL,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        DWORD written = 0;
        const bool ok = ::WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, NULL) &&
                        written == utf8.size();
        ::CloseHandle(file);
        return ok;
    }

    /** folder\stem.ext, or "stem (2).ext" and up when the name is taken. Empty if all are. */
    inline std::wstring FreePath(const std::wstring& folder, const std::wstring& stem,
                                 const std::wstring& extension)
    {
        for (int number = 1; number < 1000; ++number)
        {
            const std::wstring path = folder + L"\\" + stem +
                (number > 1 ? L" (" + std::to_wstring(number) + L")" : std::wstring()) + extension;

            if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES &&
                (::GetLastError() == ERROR_FILE_NOT_FOUND || ::GetLastError() == ERROR_PATH_NOT_FOUND))
                return path;
        }

        return std::wstring();
    }

    /**
     * Moves a finished download into folder, creating the folder when it is
     * missing, under a name nothing else has. Works across drives (it is then a
     * copy and a delete). Returns the new path, or empty with a Win32 error code.
     */
    inline std::wstring MoveIntoFolder(const std::wstring& source, const std::wstring& folder,
                                       const std::wstring& stem, const std::wstring& extension, DWORD& error)
    {
        error = ERROR_SUCCESS;

        const int created = ::SHCreateDirectoryExW(NULL, folder.c_str(), NULL);
        if (created != ERROR_SUCCESS && created != ERROR_ALREADY_EXISTS && created != ERROR_FILE_EXISTS)
        {
            error = (DWORD)created;
            return std::wstring();
        }

        const DWORD attributes = ::GetFileAttributesW(folder.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            error = ERROR_DIRECTORY;
            return std::wstring();
        }

        const std::wstring target = FreePath(folder, stem, extension);
        if (target.empty())
        {
            error = ERROR_FILE_EXISTS;
            return std::wstring();
        }

        if (!::MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH))
        {
            error = ::GetLastError();
            return std::wstring();
        }

        return target;
    }

    // ------------------------------------------------------------------ sign-out

    /** The hand-exported cookies BuildCookieStrategies (browser_cookies.h) picks up. */
    inline std::wstring CookiesFile(const std::wstring& workDirectory)
    {
        return workDirectory + L"\\cookies.txt";
    }

    /** True while there is anything a sign-out would remove. */
    inline bool HasAccounts(const std::wstring& workDirectory)
    {
        return ::GetFileAttributesW(CookiesFile(workDirectory).c_str()) != INVALID_FILE_ATTRIBUTES ||
               ::GetFileAttributesW(LoginProfileDirectory(workDirectory).c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    namespace detail
    {
        /**
         * Deletes a folder and everything in it. A link inside is removed, never
         * followed. True when nothing is left.
         */
        inline bool DeleteTree(const std::wstring& path)
        {
            WIN32_FIND_DATAW data;
            HANDLE find = ::FindFirstFileW((path + L"\\*").c_str(), &data);
            if (find != INVALID_HANDLE_VALUE)
            {
                do
                {
                    const std::wstring name = data.cFileName;
                    if (name == L"." || name == L"..")
                        continue;

                    const std::wstring child = path + L"\\" + name;
                    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    {
                        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                            ::RemoveDirectoryW(child.c_str());
                        else
                            DeleteTree(child);
                    }
                    else
                    {
                        if (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                            ::SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                        ::DeleteFileW(child.c_str());
                    }
                }
                while (::FindNextFileW(find, &data));
                ::FindClose(find);
            }

            ::RemoveDirectoryW(path.c_str());
            return ::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
        }
    }

    /**
     * Forgets every account the plugin could use. False when something could
     * not be deleted - in practice a sign-in browser window still open on the
     * profile.
     */
    inline bool SignOut(const std::wstring& workDirectory)
    {
        const std::wstring cookies = CookiesFile(workDirectory);
        if (::GetFileAttributesW(cookies.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            ::SetFileAttributesW(cookies.c_str(), FILE_ATTRIBUTE_NORMAL);
            ::DeleteFileW(cookies.c_str());
        }

        const std::wstring profile = LoginProfileDirectory(workDirectory);
        if (::GetFileAttributesW(profile.c_str()) != INVALID_FILE_ATTRIBUTES)
            detail::DeleteTree(profile);

        return !HasAccounts(workDirectory);
    }
}

#endif // YOUTUBE_OUTPUT_FOLDER_H

#pragma once

#include <Windows.h>
#include <ShlObj.h>
#include <string>
#include <vector>

#pragma comment(lib, "version.lib")

namespace NSYoutube
{
    inline bool HasMinimumFileVersion(const std::wstring& path, DWORD minimumMS, DWORD minimumLS)
    {
        DWORD unused = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &unused);
        if (!size)
            return false;

        std::vector<BYTE> data(size);
        if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
            return false;

        VS_FIXEDFILEINFO* info = nullptr;
        UINT length = 0;
        if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) ||
            !info || length < sizeof(VS_FIXEDFILEINFO) || info->dwSignature != 0xFEEF04BD)
            return false;

        return info->dwFileVersionMS > minimumMS ||
            (info->dwFileVersionMS == minimumMS && info->dwFileVersionLS >= minimumLS);
    }

    inline bool HasMediaToolkitLibraries()
    {
        // CSIDL_PROGRAM_FILES_COMMON follows the process architecture, just like
        // the bundled AVSMediaToolkit. Never accept the other architecture's DLLs.
        wchar_t commonFiles[MAX_PATH]{};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES_COMMON, nullptr,
            SHGFP_TYPE_CURRENT, commonFiles)))
            return false;

        const std::wstring directory = std::wstring(commonFiles) + L"\\AVSMedia\\ActiveX\\";
        return HasMinimumFileVersion(directory + L"AVSMatroskaFile3.dll", MAKELONG(0, 6), MAKELONG(1061, 0)) &&
            HasMinimumFileVersion(directory + L"AVSQuickTimeFile3.dll", MAKELONG(0, 6), MAKELONG(343, 0));
    }
}

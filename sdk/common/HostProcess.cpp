#include "HostProcess.h"

#include <Windows.h>

#include <cwchar>
#include <cwctype>

namespace NSHostProcess
{
    std::wstring GetHostProcess()
    {
        wchar_t executablePath[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executablePath,
            ARRAYSIZE(executablePath));
        if (length == 0 || length >= ARRAYSIZE(executablePath))
            return {};

        const wchar_t* fileName = executablePath;
        if (const wchar_t* slash = std::wcsrchr(executablePath, L'\\'))
            fileName = slash + 1;

        std::wstring normalized;
        for (const wchar_t ch : std::wstring(fileName))
        {
            if (std::iswalnum(ch))
                normalized.push_back(static_cast<wchar_t>(std::towlower(ch)));
        }

        return normalized;
    }
}
#include "pch.h"
#include "export_utils.h"
#include <cwchar>
wchar_t* ExportString(const wchar_t* value) {
    if (!value) return nullptr;
    const size_t size = std::wcslen(value) + 1;
    auto* result = new wchar_t[size];
    wcscpy_s(result, size, value);
    return result;
}
void ReleaseExportString(const wchar_t* value) { delete[] value; }

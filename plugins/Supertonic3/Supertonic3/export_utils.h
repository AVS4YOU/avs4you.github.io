#pragma once
#include <windows.h>
wchar_t* ExportString(const wchar_t* value);
void ReleaseExportString(const wchar_t* value);

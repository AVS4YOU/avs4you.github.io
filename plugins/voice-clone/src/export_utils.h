#pragma once
#include <string>
#include <windows.h>
wchar_t* export_str(const wchar_t* value);
void release_export_ptr(const wchar_t* value);
std::wstring Translate(const wchar_t* value);

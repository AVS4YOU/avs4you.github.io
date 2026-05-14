#pragma once
#include <windows.h>

WCHAR* export_str(const WCHAR* ptr);
void release_export_ptr(const WCHAR* ptr);
wchar_t* TR(const wchar_t* name);
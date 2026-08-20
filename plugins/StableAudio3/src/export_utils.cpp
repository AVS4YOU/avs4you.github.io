#include "export_utils.h"
#include <cwchar>
wchar_t* export_str(const wchar_t* value) {
	if (!value)
		return nullptr;
	const size_t n = std::wcslen(value) + 1;
	auto* result = new wchar_t[n];
	wcscpy_s(result, n, value);
	return result;
}
void release_export_ptr(const wchar_t* value) { delete[] value; }

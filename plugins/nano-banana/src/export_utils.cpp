#include "export_utils.h"
#include "../../../sdk/translate/translate.h"

WCHAR* export_str(const WCHAR* ptr)
{
	if (ptr == NULL)
		return NULL;

	size_t len = wcslen(ptr);
	WCHAR* result = new WCHAR[len + 1];

	if (result == NULL)
		return NULL;

	wcscpy_s(result, len + 1, ptr);
	return result;
}
void release_export_ptr(const WCHAR* ptr)
{
	delete[] ptr;
}
wchar_t* TR(const wchar_t* name)
{
	std::wstring item = CTranslate::GetInstance().GetManager()->Translate(name);
	return export_str(item.c_str());
}
#include <windows.h>
#include "resource.h"
#include "../../../sdk/translate/translate.h"
HMODULE g_hInst = nullptr;
BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_hInst = h;
		CTranslate::GetInstance().Init(g_hInst, IDR_TRANSLATION);
		DisableThreadLibraryCalls(h);
	}
	return TRUE;
}

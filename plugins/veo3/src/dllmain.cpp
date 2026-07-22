// dllmain.cpp : Defines the entry point for the DLL application.
#include "exports.h"
#include "plugin.h"
#include "resource.h"
#include "../../../sdk/include/CContentPluginIntf.h"
#include "../../../sdk/translate/translate.h"

HMODULE g_hInst = NULL;
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH: {
		g_hInst = hModule;
		CTranslate::GetInstance().Init(g_hInst, IDR_TRANSLATION);
		break;
	}
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
	{
		const wchar_t* windows[] = {
			L"Veo3MainWindowClass",
			L"Veo3SettingsWindowClass",
			nullptr
		};
		for (const wchar_t** cur = windows; *cur != nullptr; ++cur)
		{
			WNDCLASSEX wc = { 0 };
			wc.cbSize = sizeof(WNDCLASSEX);
			if (GetClassInfoEx(GetModuleHandle(NULL), *cur, &wc))
			{
				HBRUSH hBrush = wc.hbrBackground;
				UnregisterClass(*cur, GetModuleHandle(NULL));
				if (hBrush)
					DeleteObject(hBrush);
			}
		}
		break;
	}
	}
	return TRUE;
}

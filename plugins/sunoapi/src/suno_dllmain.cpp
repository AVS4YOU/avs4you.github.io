#include <windows.h>
HMODULE g_hInst = nullptr;
BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_hInst = h;
		DisableThreadLibraryCalls(h);
	}
	return TRUE;
}

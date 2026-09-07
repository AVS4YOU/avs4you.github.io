#include "../../../sdk/translate/translate.h"
#include "plugin.h"
#include "resource.h"
HMODULE g_module = nullptr;
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_module = module;
		CTranslate::GetInstance().Init(module, IDR_TRANSLATION);
		DisableThreadLibraryCalls(module);
	}
	return TRUE;
}

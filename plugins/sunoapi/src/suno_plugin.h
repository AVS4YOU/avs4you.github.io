#pragma once
#include <string>
#include <vector>
#include <windows.h>
#define PLUGIN_EXPORTS
#include "../../../sdk/include/CContentPluginIntf.h"
extern HMODULE g_hInst;
class CSunoApiPlugin
{
public:
	std::wstring workDirectory;
	HWND parentWindow = nullptr;
	HWND window = nullptr;
	HANDLE activationContext = INVALID_HANDLE_VALUE;
	ULONG_PTR activationCookie = 0;
	DWORD activationThreadId = 0;
	AsyncCallback callback = nullptr;
	void *callbackContext = nullptr;
	CSunoApiPlugin();
	~CSunoApiPlugin() = default;
};

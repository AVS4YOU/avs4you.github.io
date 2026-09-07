#pragma once
#include <windows.h>
#include "../../../sdk/include/CContentPluginIntf.h"
#include "../../../sdk/translate/translate.h"

#include "resource.h"
#include "nano-banana.h"

#include "nano-banana_utils.h"

extern HMODULE g_hInst;

// Contains the Nano Banana engine and has a callback function to handle responses
class CNanoBananaPlugin : public NSProcesses::CProcessRunnerCallback
{
public:
	CNanoBanana m_engine;

	std::wstring m_workDirectory;
	HWND m_hWindow;
	HWND m_hParentWindow = NULL;

	// use in call after work
	AsyncCallback m_callback = nullptr;
	void* m_callbackContext = nullptr;

public:
	CNanoBananaPlugin();
	CNanoBananaPlugin(const CNanoBananaPlugin&) = delete;
	CNanoBananaPlugin(CNanoBananaPlugin&&) = delete;
	virtual ~CNanoBananaPlugin();

	virtual Plugins::PluginType Type() const;
	virtual void ProcessCallback(const int& id, const NSProcesses::StreamType& type, const std::string& message);
};


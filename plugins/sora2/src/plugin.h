#pragma once
#include "../../../sdk/include/CContentPluginIntf.h"
#include "../../../sdk/translate/translate.h"

#include "resource.h"
#include "sora2.h"

#include "utils.h"
#include <windows.h>

extern HMODULE g_hInst;

// Contains the Sora engine and has a callback function to handle responses
class CSoraPlugin : public NSProcesses::CProcessRunnerCallback
{
public:
	CSora2 m_engine;

	std::wstring m_workDirectory;
	HWND m_hWindow;
	HWND m_hParentWindow = NULL;

	// use in call after work
	AsyncCallback m_callback = nullptr;
	void* m_callbackContext = nullptr;

public:
	CSoraPlugin();
	CSoraPlugin(const CSoraPlugin&) = delete;
	CSoraPlugin(CSoraPlugin&&) = delete;
	virtual ~CSoraPlugin();

	virtual Plugins::PluginType Type() const;
	virtual void ProcessCallback(const int& id, const NSProcesses::StreamType& type, const std::string& message);
};
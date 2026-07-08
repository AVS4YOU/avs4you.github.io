#pragma once
#include <windows.h>
#include "../../../sdk/include/CContentPluginIntf.h"
#include "../../../sdk/translate/translate.h"

#include "resource.h"
#include "veo3.h"

#include "veo3_utils.h"

extern HMODULE g_hInst;

// Contains the Veo3 engine and has a callback function to handle responses
class CVeo3Plugin : public NSProcesses::CProcessRunnerCallback
{
public:
	CVeo3 m_engine;

	std::wstring m_workDirectory;
	HWND m_hWindow;
	HWND m_hParentWindow = NULL;

	// use in call after work
	AsyncCallback m_callback = nullptr;
	void* m_callbackContext = nullptr;

public:
	CVeo3Plugin();
	CVeo3Plugin(const CVeo3Plugin&) = delete;
	CVeo3Plugin(CVeo3Plugin&&) = delete;
	virtual ~CVeo3Plugin();

	virtual Plugins::PluginType Type() const;
	virtual void ProcessCallback(const int& id, const NSProcesses::StreamType& type, const std::string& message);
};


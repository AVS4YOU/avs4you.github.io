#include "plugin.h"
#include "../../../sdk/include/CContentPluginIntf.h"
#include "ShlObj.h"
#include "exports.h"
#include "resource.h"
#include "veo3.h"
#include "../../../sdk/translate/translate.h"
#include "veo3_utils.h"
#include "../../../sdk/ui/winapi/ui.h"
#include "veo3_ui.h"

CVeo3Plugin::CVeo3Plugin()
{
	wchar_t sAppDataLocal[65535];

	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, sAppDataLocal)))
		m_workDirectory = std::wstring(sAppDataLocal);
	else
		m_workDirectory = NSSystemUtils::GetTempDirectory();

	m_workDirectory += L"\\avs_plugin_veo3";
	CreateDirectoryW(m_workDirectory.c_str(), NULL);

	std::wstring iconPath = m_workDirectory + L"\\icon_internal.ico";
	std::wstring iconPathApp = m_workDirectory + L"\\icon.ico";

	if (!NSSystemUtils::ExistsFile(iconPath))
	{
		HMODULE hModule = g_hInst;
		HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(IDR_ICON), RT_RCDATA);
		if (hResource)
		{
			DWORD size = SizeofResource(hModule, hResource);
			HGLOBAL hLoaded = LoadResource(hModule, hResource);
			if (hLoaded)
			{
				void* pLocked = LockResource(hLoaded);
				if (pLocked)
				{
					HANDLE hFile = CreateFileW(iconPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
						FILE_ATTRIBUTE_NORMAL, NULL);

					if (hFile != INVALID_HANDLE_VALUE)
					{
						DWORD bytesWritten = 0;
						BOOL result = WriteFile(hFile, pLocked, static_cast<DWORD>(size), &bytesWritten, NULL);

						CloseHandle(hFile);
					}
				}
			}
		}
	}

	::CopyFileW(iconPath.c_str(), iconPathApp.c_str(), FALSE);
}

CVeo3Plugin::~CVeo3Plugin()
{
	if (NSSystemUtils::ExistsFile(m_workDirectory + L"\\script.prompt"))
		NSSystemUtils::RemoveFile(m_workDirectory + L"\\script.prompt");

	if (NSSystemUtils::ExistsFile(m_workDirectory + L"\\script.ps1"))
		NSSystemUtils::RemoveFile(m_workDirectory + L"\\script.ps1");
}

Plugins::PluginType CVeo3Plugin::Type() const
{
	return Plugins::PluginType::Content;
}

void CVeo3Plugin::ProcessCallback(const int& id, const NSProcesses::StreamType& type, const std::string& message)
{
	switch (type)
	{
	case NSProcesses::StreamType::StdOut:
	{
		MessageOutputData* data = new MessageOutputData();
		data->text = message;
		PostMessage(m_hWindow, WM_COMMAND, static_cast<WPARAM>(WmMainWindowCommands::Output), reinterpret_cast<LPARAM>(data));
		break;
	}
	case NSProcesses::StreamType::StdErr:
	{
		MessageOutputData* data = new MessageOutputData();
		data->text = "[ERROR]" + message;
		PostMessage(m_hWindow, WM_COMMAND, static_cast<WPARAM>(WmMainWindowCommands::Output), reinterpret_cast<LPARAM>(data));
		break;
	}
	case NSProcesses::StreamType::Stop:
	{
		if (m_engine.m_manager)
		{
			m_engine.m_manager->StopAll();
			PostMessage(m_hWindow, WM_COMMAND, static_cast<WPARAM>(WmMainWindowCommands::OutputStop), 0);
			break;
		}
		break;
	}
	default:
		break;
	}
}
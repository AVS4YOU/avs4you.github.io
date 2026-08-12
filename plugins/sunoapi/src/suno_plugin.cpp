#include "suno_plugin.h"
#include "suno_api.h"
#include "resource.h"
#include <ShlObj.h>
CSunoApiPlugin::CSunoApiPlugin()
{
	wchar_t dir[MAX_PATH] = {};
	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, dir)))
		workDirectory = dir;
	else
		workDirectory = L".";
	workDirectory += L"\\avs_plugin_sunoapi";
	CreateDirectoryW(workDirectory.c_str(), nullptr);

	const std::wstring internalIconPath = workDirectory + L"\\icon_internal.ico";
	const std::wstring iconPath = workDirectory + L"\\icon.ico";
	if (GetFileAttributesW(internalIconPath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		HRSRC resource = FindResourceW(g_hInst, MAKEINTRESOURCEW(IDR_ICON), RT_RCDATA);
		if (resource)
		{
			DWORD resourceSize = SizeofResource(g_hInst, resource);
			HGLOBAL loadedResource = LoadResource(g_hInst, resource);
			const void *resourceData = loadedResource ? LockResource(loadedResource) : nullptr;
			if (resourceData)
			{
				HANDLE file = CreateFileW(internalIconPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (file != INVALID_HANDLE_VALUE)
				{
					DWORD bytesWritten = 0;
					WriteFile(file, resourceData, resourceSize, &bytesWritten, nullptr);
					CloseHandle(file);
				}
			}
		}
	}
	CopyFileW(internalIconPath.c_str(), iconPath.c_str(), FALSE);
}

#include "suno_plugin.h"
#include "suno_api.h"
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
}

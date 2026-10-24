#pragma once
#include "plugin.h"

enum : UINT {
	WM_SUBTITLES_STATUS = WM_APP + 51,
};

struct UiStatus {
	std::wstring text;
	int progress;   // 0-100, or -1 to leave the bar alone
	bool finished;
	bool success;
};

// initialFile - media file from the host app (context menu), shown as the selected file
void ShowSubtitlesWindow(CSubtitlesPlugin* plugin, const std::filesystem::path& initialFile = {});

#pragma once
#include "plugin.h"

enum : UINT {
	WM_STEMS_STATUS = WM_APP + 61,
};

struct UiStatus {
	std::wstring text;
	int progress;   // 0-100, or -1 to leave the bar alone
	bool finished;
	bool success;
};

// initialFile - media file from the host app (context menu), shown as the selected file
void ShowStemSplitterWindow(CStemSplitterPlugin* plugin, const std::filesystem::path& initialFile = {});

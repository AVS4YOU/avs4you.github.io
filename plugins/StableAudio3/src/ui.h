#pragma once
#include "plugin.h"
enum : UINT {
	WM_SA3_STATUS = WM_APP + 41,
	WM_SA3_CLOSE_AFTER_CALLBACK = WM_APP + 42
};
struct UiStatus {
	std::wstring text;
	int progress;
	bool finished;
	bool success;
};
void ShowStableAudioWindow(CStableAudio3Plugin* plugin);

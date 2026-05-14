#pragma once
#include "plugin.h"

#define BASE_COLOR_RGB RGB(37, 150, 190)

class MessageOutputData
{
public:
	std::string text;
};

enum class WmMainWindowCommands : WPARAM
{
	ComboModel = 0x8001,
	ComboOrientation = 0x8002,
	ComboSize = 0x8003,
	ComboDuration = 0x8004,

	ButtonTextToVideo = 0x8104,
	ButtonImageToVideo = 0x8105,
	ButtonFirstAndLastFrame = 0x8106,
	ButtonExtendVideo = 0x8107,

	ButtonFile1 = 0x8108,
	ButtonFile2 = 0x8109,
	ButtonFile3 = 0x810A,

	ButtonGenerate = 0x8005,
	ButtonSettings = 0x8006,

	Output = 0x8007,
	OutputStop = 0x8008,
};

namespace NSUI
{
	void CenterWindow(HWND hwnd);

	LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void ShowSettingsWindow(HWND hwndParent, CVeo3Plugin* plugin);

	LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void ShowPromptWindow(CVeo3Plugin* plugin);
} // namespace NSUI
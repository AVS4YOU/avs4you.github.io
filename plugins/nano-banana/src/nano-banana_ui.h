#pragma once
#include "plugin.h"

#define BASE_COLOR_RGB RGB(255, 210, 58)

class MessageOutputData
{
public:
	std::string text;
};

enum class WmMainWindowCommands : WPARAM
{
	ComboModel = 0x8001,
	ComboAspectRatio = 0x8002,
	ComboImageSize = 0x8003,

	ButtonGenerate = 0x8005,
	ButtonSettings = 0x8006,
	Output = 0x8007,
	OutputStop = 0x8008,

	ButtonTextToImage = 0x8100,
	ButtonImageEdit = 0x8101,
	ButtonMultiTurn = 0x8102,
	ButtonGoogleSearch = 0x8103,
	ButtonImageSearch = 0x8104,
	ButtonVideoToImage = 0x8105,

	ButtonFile1 = 0x8110,
	ButtonFile2 = 0x8111,
	ButtonFile3 = 0x8112,
	ButtonFile4 = 0x8113,
	ButtonFile5 = 0x8114,
	ButtonFile6 = 0x8115,
};

namespace NSUI
{
	void CenterWindow(HWND hwnd);

	LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void ShowSettingsWindow(HWND hwndParent, CNanoBananaPlugin* plugin);

	LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void ShowPromptWindow(CNanoBananaPlugin* plugin);
} // namespace NSUI

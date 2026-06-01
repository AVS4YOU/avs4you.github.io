#include "veo3_ui.h"
#include "export_utils.h"
#include "exports.h"
#include "sha256.h"

#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#include "../../../sdk/ui/winapi/ui.h"
#include "../../../sdk/common/utils.h"
#include "../../../sdk/3dparty/nlohmann/json/single_include/nlohmann/json.hpp"

#define VEO3_SETTINGS_WINDOW_CLASS	L "Veo3SettingsWindowClass"
#define VEO3_MAIN_WINDOW_CLASS		L "Veo3MainWindowClass"

// Models Names
#define VEO_3_1			L"Veo 3.1"
#define VEO_3_1_FAST	L"Veo 3.1 Fast"
#define VEO_3_1_LITE	L"Veo 3.1 Lite"

// Models Codes
#define VEO_3_1_PREVIEW			L"veo-3.1-generate-preview"
#define VEO_3_1_FAST_PREVIEW	L"veo-3.1-fast-generate-preview"
#define VEO_3_1_LITE_PREVIEW	L"veo-3.1-lite-generate-preview"

// Video Resolution
#define RES_HD		L"720p"
#define RES_FULLHD	L"1080p"
#define RES_4K		L"4k"

// Text (routed through Translate; English literal is the lookup key)
#define FIRST_IMAGE		CTranslate::GetInstance().GetManager()->Translate(L"First Image").c_str()
#define SECOND_IMAGE	CTranslate::GetInstance().GetManager()->Translate(L"Second Image").c_str()
#define THIRD_IMAGE		CTranslate::GetInstance().GetManager()->Translate(L"Third Image").c_str()
#define FIRST_FRAME		CTranslate::GetInstance().GetManager()->Translate(L"First Frame").c_str()
#define LAST_FRAME		CTranslate::GetInstance().GetManager()->Translate(L"Last Frame").c_str()
#define PREV_VIDEO		CTranslate::GetInstance().GetManager()->Translate(L"Video from a previous generation").c_str()

static bool ParseCacheDateTime(const std::string& text, std::time_t& outTime)
{
	std::tm tm = {};
	std::istringstream ss(text);
	ss >> std::get_time(&tm, "%d.%m.%Y_%H.%M.%S");
	if (ss.fail()) return false;
	tm.tm_isdst = -1;
	outTime = std::mktime(&tm);
	return outTime != (std::time_t)-1;
}

namespace NSUI
{
	// UI
	void CenterWindow(HWND hwnd)
	{
		RECT rcWin;
		GetWindowRect(hwnd, &rcWin);

		RECT rcWork;
		if (true)
		{
			HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

			MONITORINFO mi = { sizeof(mi) };
			if (GetMonitorInfo(hMonitor, &mi))
			{
				rcWork = mi.rcWork;
			}
			else
			{
				SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);
			}
		}

		int winWidth = rcWin.right - rcWin.left;
		int winHeight = rcWin.bottom - rcWin.top;

		int x = rcWork.left + (rcWork.right - rcWork.left - winWidth) / 2;
		int y = rcWork.top + (rcWork.bottom - rcWork.top - winHeight) / 2;

		SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	}

	// ====================================== SETTINGS WINDOW ======================================
	LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		static HWND hEdit, hBtnOK, hBtnCancel, hBtnDelete;

		if (msg == WM_NCCREATE)
		{
			CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		CVeo3Plugin* plugin = (CVeo3Plugin*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

		auto closeWindow = [hwnd]() {
			HWND hwndParent = GetParent(hwnd);
			if (!hwndParent)
				hwndParent = GetWindow(hwnd, GW_OWNER);

			if (hwndParent)
			{
				EnableWindow(hwndParent, TRUE);
				SetForegroundWindow(hwndParent);
			}
			DestroyWindow(hwnd);
		};

		switch (msg)
		{
		case WM_CREATE:
		{
			hEdit = AVS::CreateTextEditMultiline(hwnd, GetModuleHandle(NULL), 15, 15, 330, 25, AVS::TextEditSettings::Create());

			std::wstring keyValue = NSSystemUtils::ReadWStringFromUtf8File(plugin->m_workDirectory + L"\\app.key");
			if (!keyValue.empty())
			{
				SetWindowText(hEdit, keyValue.c_str());
			}

			CTranslateManager* tr = CTranslate::GetInstance().GetManager();

			hBtnOK = AVS::CreateButton(hwnd, (HMENU)0x8001, GetModuleHandle(NULL), tr->Translate(L"Save").c_str(),
				265, 55, 80, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::Primary));

			hBtnCancel = AVS::CreateButton(hwnd, (HMENU)0x8002, GetModuleHandle(NULL), tr->Translate(L"Cancel").c_str(),
				175, 55, 80, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));

			hBtnDelete = AVS::CreateButton(hwnd, (HMENU)0x8003, GetModuleHandle(NULL), tr->Translate(L"Delete API-key").c_str(),
				15, 55, 120, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));

			SetWindowPos(hEdit, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hBtnDelete, hEdit, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hBtnCancel, hBtnDelete, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hBtnOK, hBtnCancel, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetFocus(hEdit);

			break;
		}
		case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
			case 0x8001: // OK
			{
				int len = GetWindowTextLength(hEdit);
				if (len > 0)
				{
					std::wstring key(len, L'\0');
					GetWindowText(hEdit, &key[0], len + 1);
					std::wstring keyFile = plugin->m_workDirectory + L"\\app.key";

					NSSystemUtils::WriteWStringToUtf8File(key, keyFile, false);
				}
				closeWindow();
				break;
			}
			case IDCANCEL:
			case 0x8002: // Cancel
			{
				closeWindow();
				break;
			}
			case 0x8003: // Delete
			{
				std::wstring keyFile = plugin->m_workDirectory + L"\\app.key";
				NSSystemUtils::RemoveFile(keyFile);
				SetWindowText(hEdit, L"");
				break;
			}
			default:
				break;
			}
			break;
		}
		case WM_CLOSE:
		{
			closeWindow();
			return 0;
		}
		default:
			break;
		}
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	void ShowSettingsWindow(HWND hwndParent, CVeo3Plugin* plugin)
	{
		const wchar_t* className = L"Veo3SettingsWindowClass";

		WNDCLASSEX wcCheck = { 0 };
		wcCheck.cbSize = sizeof(WNDCLASSEX);

		if (!GetClassInfoEx(GetModuleHandle(NULL), className, &wcCheck))
		{
			WNDCLASSEX wc = { 0 };
			wc.cbSize = sizeof(WNDCLASSEX);

			wc.lpfnWndProc = SettingsWndProc;
			wc.hInstance = GetModuleHandle(NULL);
			wc.lpszClassName = L"Veo3SettingsWindowClass";
			wc.hCursor = LoadCursor(NULL, IDC_ARROW);

			AVS::Color colorBack = AVS::Color::GetDefaultWindowBackground();
			HBRUSH hBackgroundBrush = CreateSolidBrush(RGB(colorBack.R, colorBack.G, colorBack.B));
			wc.hbrBackground = hBackgroundBrush;

			std::wstring iconPath = plugin->m_workDirectory + L"\\icon_internal.ico";

			HMODULE hModule = GetModuleHandle(NULL);
			wc.hIcon = (HICON)LoadImage(hModule, iconPath.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
			wc.hIconSm = (HICON)LoadImage(hModule, iconPath.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE | LR_DEFAULTCOLOR);

			RegisterClassEx(&wc);
		}

		DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
		std::wstring titleWindow = CTranslate::GetInstance().GetManager()->Translate(L"Settings");

		RECT rc = { 0, 0, 360, 95 };
		AdjustWindowRectEx(&rc, dwStyle, FALSE, 0);

		HWND hwnd = CreateWindowEx(0, className, titleWindow.c_str(), dwStyle, CW_USEDEFAULT, CW_USEDEFAULT,
			rc.right - rc.left, rc.bottom - rc.top, hwndParent, NULL, GetModuleHandle(NULL), plugin);

		if (hwndParent)
			EnableWindow(hwndParent, FALSE);

		CenterWindow(hwnd);
		ShowWindow(hwnd, SW_SHOW);
		UpdateWindow(hwnd);
	}
	int activeNow = static_cast<int>(WmMainWindowCommands::ButtonTextToVideo);

	// ====================================== MAIN WINDOW ======================================
	LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		static HWND hMainEdit,
			hModel, hOrientation, hSize, hDuration,
			hProgress,
			hFile1, hFile2, hFile3,
			hPathFile1, hPathFile2, hPathFile3,
			hStatus,
			hTextToVideo,
			hImageToVideo,
			hFirstAndLastFrame,
			hExtendVideo,
			hSettings, hGenerate;

		RECT rcMain;
		GetWindowRect(hwnd, &rcMain);

		bool run = true;
		int fakeProgress = 0;
		std::vector<HWND*> vFile =
		{
			&hFile1,
			&hFile2,
			&hFile3,
		};

		std::vector<HWND*> vFilePaths =
		{
			&hPathFile1,
			&hPathFile2,
			&hPathFile3,
		};

		std::vector<HWND*> vToggleGroup =
		{
			&hTextToVideo,
			&hImageToVideo,
			&hFirstAndLastFrame,
			&hExtendVideo
		};

		CTranslateManager* trToggle = CTranslate::GetInstance().GetManager();
		std::vector<std::wstring> vToggleGroupNames =
		{
			trToggle->Translate(L"Text to Video"),
			trToggle->Translate(L"Image to Video"),
			trToggle->Translate(L"First + Last Frame"),
			trToggle->Translate(L"Extend Video")
		};

		if (msg == WM_NCCREATE)
		{
			CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		CVeo3Plugin* plugin = (CVeo3Plugin*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

		switch (msg)
		{
		case WM_CREATE:
		{
			int SettingsButtonsW = 100;
			int WindowW = 500;

			// Prompt edit
			AVS::TextEditSettings mainEditSettings = AVS::TextEditSettings::Create();
			mainEditSettings.IsMultiline = true;
			mainEditSettings.IsVscroll = true;
			hMainEdit = AVS::CreateTextEditMultiline(hwnd, GetModuleHandle(NULL), 15, 45, WindowW - 30 - 10 - SettingsButtonsW, 200, mainEditSettings);

			CTranslateManager* tr = CTranslate::GetInstance().GetManager();
			HINSTANCE hInstance = GetModuleHandle(NULL);

			// Toggle Group
			hTextToVideo = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonTextToVideo, hInstance,
				tr->Translate(L"Text to Video").c_str(),
				15, 15, 80, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupEnable));

			hImageToVideo = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonImageToVideo, hInstance,
				tr->Translate(L"Image to Video").c_str(),
				15 + (80 * 1), 15, 100, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable));

			hFirstAndLastFrame = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonFirstAndLastFrame, hInstance,
				tr->Translate(L"First + Last Frame").c_str(),
				15 + (80 * 2) + 20, 15, 100, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable));

			hExtendVideo = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonExtendVideo, hInstance,
				tr->Translate(L"Extend Video").c_str(),
				15 + (80 * 3) + 20 + 20, 15, 80, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable));

			// File Buttons
			int file1h = 250;
			int file2h = 250 + 25 + 8;
			int file3h = 250 + 50 + 16;

			hFile1 = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonFile1, hInstance,
				tr->Translate(L"Add File").c_str(),
				15, 250, 80, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));
			hFile2 = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonFile2, hInstance,
				tr->Translate(L"Add File").c_str(),
				15, 250 + 25 + 8, 80, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));
			hFile3 = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonFile3, hInstance,
				tr->Translate(L"Add File").c_str(),
				15, 250 + 50 + 16, 80, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));

			hPathFile1 = AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Add file #1").c_str(), 100, file1h, 380, 25, AVS::LabelSettings::Create(AVS::LabelType::Enabled));
			hPathFile2 = AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Add file #2").c_str(), 100, file2h, 380, 25, AVS::LabelSettings::Create(AVS::LabelType::Enabled));
			hPathFile3 = AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Add file #3").c_str(), 100, file3h, 380, 25, AVS::LabelSettings::Create(AVS::LabelType::Enabled));
			
			ShowWindow(hFile1, SW_HIDE);
			ShowWindow(hFile2, SW_HIDE);
			ShowWindow(hFile3, SW_HIDE);

			ShowWindow(hPathFile1, SW_HIDE);
			ShowWindow(hPathFile2, SW_HIDE);
			ShowWindow(hPathFile3, SW_HIDE);

			int left = WindowW - 15 - SettingsButtonsW;
			int top = 15;
			int labelH = 25;
			int distance = 0;

			// Model LABLE
			HWND hModelLabel = AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Model").c_str(),
				left, top, SettingsButtonsW, labelH, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
			top += labelH;

			// MODEL Combo Box
			hModel = AVS::CreateComboBox(hwnd, (HMENU)WmMainWindowCommands::ComboModel, hInstance,
				left, top, SettingsButtonsW, 25, AVS::ComboBoxSettings::Create(),
				{ VEO_3_1, VEO_3_1_FAST, VEO_3_1_LITE });
			top += (distance + 25);

			// Orientation LABLE
			HWND hOrientationLabel = AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Orientation").c_str(),
				left, top, SettingsButtonsW, 25, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
			top += labelH;

			// Orientation Combo Box
			hOrientation = AVS::CreateComboBox(hwnd, (HMENU)WmMainWindowCommands::ComboOrientation, hInstance,
				left, top, SettingsButtonsW, 25, AVS::ComboBoxSettings::Create(),
				{ L"16:9", L"9:16" });
			top += (distance + 25);

			// Resolution Lable
			HWND hSizeLabel = AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Size").c_str(),
				left, top, SettingsButtonsW, 25, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
			top += labelH;

			// Resolution Combo Box

			// Veo 3.1 can also directly generate 720p, 1080p or 4k videos (4k not available for Veo 3.1 Lite).
			// Note that the higher the resolution, the higher the latency will be. 4k videos are also more pricey (cf. pricing).
			// Video extension is also limited to 720p videos.

			hSize = AVS::CreateComboBox(hwnd, (HMENU)WmMainWindowCommands::ComboSize, hInstance,
				left, top, SettingsButtonsW, 25, AVS::ComboBoxSettings::Create(),
				{ RES_HD, RES_FULLHD, RES_4K });
			top += (distance + 25);

			HWND hDurationLabel = AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Duration (s)").c_str(),
				left, top, SettingsButtonsW, 25, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
			top += labelH;

			hDuration = AVS::CreateComboBox(hwnd, (HMENU)WmMainWindowCommands::ComboDuration, hInstance,
				left, top, SettingsButtonsW, 25, AVS::ComboBoxSettings::Create(),
				{ L"4", L"6", L"8" });
			top += (distance + 25);

			hProgress = AVS::CreateProgressBar(hwnd, GetModuleHandle(NULL), 15, 345, WindowW - 30, 25, AVS::ProgressBarSettings::Create());
			ShowWindow(hProgress, SW_HIDE);

			hStatus = AVS::CreateLabel(hwnd, hInstance, L"",
				15, 375, WindowW - 215, 25, AVS::LabelSettings::Create(AVS::LabelType::Enabled));

			hSettings = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonSettings, hInstance,
				tr->Translate(L"Settings").c_str(),
				320, 375, 80, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));

			hGenerate = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonGenerate, hInstance,
				tr->Translate(L"Generate").c_str(),
				410, 375, 80, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::Primary));

			// Tabs
			SetWindowPos(hMainEdit, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hTextToVideo, hMainEdit, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hImageToVideo, hMainEdit, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hFirstAndLastFrame, hMainEdit, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hExtendVideo, hMainEdit, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hModel, hMainEdit, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hOrientation, hModel, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hSize, hOrientation, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hDuration, hSize, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

			SetWindowPos(hFile1, hSize, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hPathFile1, hFile1, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hFile2, hPathFile2, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hFile3, hFile2, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

			SetWindowPos(hSettings, hDuration, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hGenerate, hSettings, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetFocus(hMainEdit);

			plugin->m_hWindow = hwnd;
			break;
		}
		case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
			case static_cast<int>(WmMainWindowCommands::ButtonGenerate):
			{
				if (plugin->m_engine.m_manager && 0 < plugin->m_engine.m_manager->GetTasksCount())
				{
					plugin->m_engine.m_manager->StopAll();

					AVS::Button_SetSettings(hGenerate, AVS::ButtonSettings::Create(AVS::Buttons::Primary),
						CTranslate::GetInstance().GetManager()->Translate(L"Generate"));

					ShowWindow(hProgress, SW_HIDE);
					break;
				}

				// Prepare Input Data
				// PROMPT
				std::wstring promptValue = L"";

				int promptLen = GetWindowTextLength(hMainEdit);
				if (promptLen > 0)
				{
					std::wstring prompt(promptLen, L'\0');
					GetWindowText(hMainEdit, &prompt[0], promptLen + 1);
					promptValue = std::move(prompt);
				}
				// API KEY
				std::wstring keyValue = NSSystemUtils::ReadWStringFromUtf8File(plugin->m_workDirectory + L"\\app.key");
				if (keyValue.empty())
				{
					ShowSettingsWindow(hwnd, plugin);
					break;
				}
				else if (!promptValue.empty())
				{
					plugin->m_engine.m_prompt = promptValue;
					plugin->m_engine.m_key = keyValue;
				}

				// ASPECT + RESOLUTION + DURATION
				plugin->m_engine.m_aspectRatio = AVS::ComboBox_GetCurrentText(hOrientation);
				plugin->m_engine.m_resolution = AVS::ComboBox_GetCurrentText(hSize);;
				plugin->m_engine.m_durationSeconds = AVS::ComboBox_GetCurrentText(hDuration);

				// MODEL
				int model = AVS::ComboBox_GetCurrent(hModel);
				if (model == 0)			// VEO_3_1
					plugin->m_engine.m_model = VEO_3_1_PREVIEW;
				else if (model == 1)	// VEO_3_1_FAST
					plugin->m_engine.m_model = VEO_3_1_FAST_PREVIEW;
				else					// VEO_3_1_LITE
					plugin->m_engine.m_model = VEO_3_1_LITE_PREVIEW;

				// GENERATION TYPE + INPUT FILES
				plugin->m_engine.m_generation_mode = NSGenerationMode::TextToVideo;
				plugin->m_engine.m_additional_files_paths.clear();
				switch (activeNow)
				{
				case static_cast<int>(WmMainWindowCommands::ButtonImageToVideo):
				{
					// 1-3 Images
					auto file1 = AVS::Label_GetText(hPathFile1);
					auto file2 = AVS::Label_GetText(hPathFile2);
					auto file3 = AVS::Label_GetText(hPathFile3);

					if (file1 != FIRST_IMAGE)	plugin->m_engine.m_additional_files_paths.push_back(file1);
					if (file2 != SECOND_IMAGE)	plugin->m_engine.m_additional_files_paths.push_back(file2);
					if (file3 != THIRD_IMAGE)	plugin->m_engine.m_additional_files_paths.push_back(file3);
					
					plugin->m_engine.m_generation_mode = NSGenerationMode::ImageToVideo;
					break;
				}
				case static_cast<int>(WmMainWindowCommands::ButtonFirstAndLastFrame):
				{
					// 2 Images
					auto file1 = AVS::Label_GetText(hPathFile1);
					auto file2 = AVS::Label_GetText(hPathFile2);

					if (file1 != FIRST_FRAME) plugin->m_engine.m_additional_files_paths.push_back(file1);
					if (file2 != LAST_FRAME) plugin->m_engine.m_additional_files_paths.push_back(file2);
					plugin->m_engine.m_generation_mode = NSGenerationMode::FirstAndLastFrame;
					break;
				}
				case static_cast<int>(WmMainWindowCommands::ButtonExtendVideo):
				{
					// 1 Video
					/* Each extension adds 7 seconds to the video
					Can chain up to 20 times (max ~148 seconds total)
					Videos stored on server for 2 days - must extend within this window
					aspectRatio and resolution must match the original video */
					run = false;
					auto file1 = AVS::Label_GetText(hPathFile1);
					if (file1 != PREV_VIDEO)
					{
						std::string hash;
						hash = CalcFileSHA256(file1);

						std::wstring json_path = plugin->m_workDirectory + L"\\cache.json";
						std::wstring date_time;
						std::string video_hash;
						std::string uri;

						bool found = false;
						bool valid_48h = false;

						DWORD attr = GetFileAttributesW(json_path.c_str());

						if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
						{
							AVS::Label_SetTextAndColor(hStatus, CTranslate::GetInstance().GetManager()->Translate(L"Unable to open file cache.json"), AVS::Color::MakeRGBA(255, 0, 0));
							break;
						}
						else
						{
							nlohmann::json cacheJson;

							try
							{
								std::ifstream in(json_path);
								if (!in.is_open())
									throw std::runtime_error("Fail opent cache.json");

								in >> cacheJson;
								for (const auto& item : cacheJson)
								{
								
									if (!item.contains("video_hash") || !item["video_hash"].is_string() ||
										!item.contains("date_time") || !item["date_time"].is_string()	||
										!item.contains("uri") || !item["uri"].is_string() || 
										!item.contains("aspectRatio") || !item["aspectRatio"].is_string() ||
										!item.contains("resolution") || !item["resolution"].is_string())
										continue;

									std::string item_hash = item["video_hash"].get<std::string>();

									if (item_hash != hash)
										continue;

									if (item["aspectRatio"] != plugin->m_engine.m_aspectRatio || 
										item["resolution"] != plugin->m_engine.m_resolution)
										throw std::runtime_error(NSStringUtils::wstring_to_utf8(CTranslate::GetInstance().GetManager()->Translate(L"aspectRatio and resolution must match the original video")));

									found = true;

									std::string date_time_utf8 = item["date_time"].get<std::string>();
									uri = item["uri"].get<std::string>();

									std::time_t cacheTime = 0;
									ParseCacheDateTime(date_time_utf8, cacheTime);

									std::time_t now = std::time(nullptr);
									double diffSeconds = std::difftime(now, cacheTime);

									if (diffSeconds >= 0 && diffSeconds <= 48.0 * 60.0 * 60.0)
									{
										valid_48h = true;
										date_time = NSStringUtils::utf8_to_wstring(date_time_utf8);
									}
									break;
								}
							}
							catch (const std::exception& e)
							{
								std::string msg = e.what();
								AVS::Label_SetTextAndColor(hStatus, NSStringUtils::utf8_to_wstring(msg), AVS::Color::MakeRGBA(255, 0, 0));
								break;
							}
							catch (...)
							{
								AVS::Label_SetTextAndColor(hStatus, NSStringUtils::utf8_to_wstring("[cache.json] Unknown exception"), AVS::Color::MakeRGBA(255, 0, 0));
								break;
							}
						}

						if (found && valid_48h)
						{
							plugin->m_engine.m_additional_files_paths.push_back(NSStringUtils::utf8_to_wstring(uri));
							run = true;
						} 
						else if (!found)
						{
							AVS::Label_SetTextAndColor(hStatus, CTranslate::GetInstance().GetManager()->Translate(L"Could not find cache for this file."), AVS::Color::MakeRGBA(255, 0, 0));
						}
						else if (!valid_48h)
						{
							AVS::Label_SetTextAndColor(hStatus, CTranslate::GetInstance().GetManager()->Translate(L"The video is outdated"), AVS::Color::MakeRGBA(255, 0, 0));
						}
					};
					
					plugin->m_engine.m_generation_mode = NSGenerationMode::ExtendVideo;
					break;
				}
				default:
					break;
				}
				
				if (!run) break;

				// UI
				AVS::Button_SetSettings(hGenerate, AVS::ButtonSettings::Create(AVS::Buttons::Default),
					CTranslate::GetInstance().GetManager()->Translate(L"Cancel"));

				AVS::Label_SetText(hStatus, L"");
				ShowWindow(hProgress, SW_SHOW);

				plugin->m_engine.Process(plugin, plugin->m_workDirectory);
				break;
			}
			case static_cast<int>(WmMainWindowCommands::ButtonSettings):
			{
				ShowSettingsWindow(hwnd, plugin);
				break;
			}
			case static_cast<int>(WmMainWindowCommands::ButtonTextToVideo):
			{
				auto activeBtnHndl =
					vToggleGroup[activeNow - static_cast<int>(WmMainWindowCommands::ButtonTextToVideo)];

				AVS::Button_SetSettings(
					*activeBtnHndl,
					AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable),
					vToggleGroupNames[activeNow - static_cast<int>(WmMainWindowCommands::ButtonTextToVideo)]);

				AVS::Button_SetSettings(
					hTextToVideo,
					AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupEnable),
					CTranslate::GetInstance().GetManager()->Translate(L"Text to Video"));

				ShowWindow(hFile1, SW_HIDE);
				ShowWindow(hFile2, SW_HIDE);
				ShowWindow(hFile3, SW_HIDE);

				ShowWindow(hPathFile1, SW_HIDE);
				ShowWindow(hPathFile2, SW_HIDE);
				ShowWindow(hPathFile3, SW_HIDE);
				

				AVS::ComboBox_SetItems(hSize, { RES_HD, RES_FULLHD, RES_4K}, 0);
				AVS::ComboBox_SetItems(hDuration, { L"4", L"6", L"8" }, 0);

				activeNow = static_cast<int>(LOWORD(wParam));
				break;
			}
			case static_cast<int>(WmMainWindowCommands::ButtonImageToVideo):
			{
				auto activeBtnHndl =
					vToggleGroup[activeNow - static_cast<int>(WmMainWindowCommands::ButtonTextToVideo)];

				AVS::Button_SetSettings(
					*activeBtnHndl,
					AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable),
					vToggleGroupNames[activeNow - static_cast<int>(WmMainWindowCommands::ButtonTextToVideo)]);

				AVS::Button_SetSettings(
					hImageToVideo,
					AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupEnable),
					CTranslate::GetInstance().GetManager()->Translate(L"Image to Video"));

				ShowWindow(hFile1, SW_SHOW);
				ShowWindow(hFile2, SW_SHOW);
				ShowWindow(hFile3, SW_SHOW);

				AVS::Label_SetText(hPathFile1, FIRST_IMAGE);
				AVS::Label_SetText(hPathFile2, SECOND_IMAGE);
				AVS::Label_SetText(hPathFile3, THIRD_IMAGE);

				ShowWindow(hPathFile1, SW_SHOW);
				ShowWindow(hPathFile2, SW_SHOW);
				ShowWindow(hPathFile3, SW_SHOW);

				AVS::ComboBox_SetItems(hSize, { RES_HD, RES_FULLHD, RES_4K }, 0);
				AVS::ComboBox_SetItems(hDuration, { L"8" }, 0);

				activeNow = static_cast<int>(LOWORD(wParam));
				break;
			}

			case static_cast<int>(WmMainWindowCommands::ButtonFirstAndLastFrame):
			{
				auto activeBtnHndl =
					vToggleGroup[activeNow - static_cast<int>(WmMainWindowCommands::ButtonTextToVideo)];

				AVS::Button_SetSettings(
					*activeBtnHndl,
					AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable),
					vToggleGroupNames[activeNow - static_cast<int>(WmMainWindowCommands::ButtonTextToVideo)]);

				AVS::Button_SetSettings(
					hFirstAndLastFrame,
					AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupEnable),
					CTranslate::GetInstance().GetManager()->Translate(L"First + Last Frame"));

				ShowWindow(hFile1, SW_SHOW);
				ShowWindow(hFile2, SW_SHOW);
				ShowWindow(hFile3, SW_HIDE);

				AVS::Label_SetText(hPathFile1, FIRST_FRAME);
				AVS::Label_SetText(hPathFile2, LAST_FRAME);

				ShowWindow(hPathFile1, SW_SHOW);
				ShowWindow(hPathFile2, SW_SHOW);
				ShowWindow(hPathFile3, SW_HIDE);

				AVS::ComboBox_SetItems(hSize, { RES_HD, RES_FULLHD, RES_4K }, 0);
				AVS::ComboBox_SetItems(hDuration, { L"8" }, 0);

				activeNow = static_cast<int>(LOWORD(wParam));
				break;
			}
			case static_cast<int>(WmMainWindowCommands::ButtonExtendVideo):
			{
				auto activeBtnHndl =
					vToggleGroup[activeNow - static_cast<int>(WmMainWindowCommands::ButtonTextToVideo)];

				AVS::Button_SetSettings(
					*activeBtnHndl,
					AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable),
					vToggleGroupNames[activeNow - static_cast<int>(WmMainWindowCommands::ButtonTextToVideo)]);

				AVS::Button_SetSettings(
					hExtendVideo,
					AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupEnable),
					CTranslate::GetInstance().GetManager()->Translate(L"Extend Video"));

				ShowWindow(hFile1, SW_SHOW);
				ShowWindow(hFile2, SW_HIDE);
				ShowWindow(hFile3, SW_HIDE);

				AVS::Label_SetText(hPathFile1, PREV_VIDEO);

				ShowWindow(hPathFile1, SW_SHOW);
				ShowWindow(hPathFile2, SW_HIDE);
				ShowWindow(hPathFile3, SW_HIDE);

				AVS::ComboBox_SetItems(hSize, { RES_HD }, 0);
				AVS::ComboBox_SetItems(hDuration, { L"4", L"6", L"8" }, 0);

				activeNow = static_cast<int>(LOWORD(wParam));
				break;
			}
			case static_cast<int>(WmMainWindowCommands::ButtonFile1):
			case static_cast<int>(WmMainWindowCommands::ButtonFile2):
			case static_cast<int>(WmMainWindowCommands::ButtonFile3):
			{
				wchar_t path[MAX_PATH] = {};

				OPENFILENAME ofn = {};
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = hwnd;
				ofn.lpstrFile = path;
				ofn.nMaxFile = MAX_PATH;
				ofn.lpstrFilter =
					L"Image files (*.png;*.jpg;*.jpeg;*.webp)\0"
					L"*.png;*.jpg;*.jpeg;*.webp\0"
					L"PNG (*.png)\0"
					L"*.png\0"
					L"JPEG (*.jpg;*.jpeg)\0"
					L"*.jpg;*.jpeg\0"
					L"WebP (*.webp)\0"
					L"*.webp\0"
					L"All files (*.*)\0"
					L"*.*\0";
				ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

				if (activeNow == static_cast<int>(WmMainWindowCommands::ButtonExtendVideo))
				{
					ofn.lpstrFilter =
						L"Video files (*.mp4;)\0"
						L"*.mp4;\0"
						L"All files (*.*)\0"
						L"*.*\0";
				}

				int iqwe = static_cast<int>(LOWORD(wParam));
				int iaqwe = static_cast<int>(WmMainWindowCommands::ButtonFile1);
				auto hFile = vFile[static_cast<int>(LOWORD(wParam)) - static_cast<int>(WmMainWindowCommands::ButtonFile1)];
				auto hFilePath = vFilePaths[static_cast<int>(LOWORD(wParam)) - static_cast<int>(WmMainWindowCommands::ButtonFile1)];

				if (GetOpenFileNameW(&ofn))
				{
					AVS::Label_SetText(*hFilePath, path);
				}
				break;
			}
			case static_cast<int>(WmMainWindowCommands::ComboSize):
			{
				int nSize = AVS::ComboBox_GetCurrent(hSize);
				switch (nSize)
				{
				case 1: // 1080p
				case 2: // 4K
				{
					// Must be "8" when using extension, reference images or with 1080p and 4k resolutions
					AVS::ComboBox_SetItems(hDuration, { L"8" }, 0);
					break;
				}
				default:
					AVS::ComboBox_SetItems(hDuration, { L"4", L"6", L"8"}, 0);
					break;
				}
				
				break;
			}
			case static_cast<int>(WmMainWindowCommands::ComboModel):
			{
				int nModel = AVS::ComboBox_GetCurrent(hModel);
				switch (nModel)
				{
				case 0:
				case 1:
				{
					AVS::ComboBox_SetItems(hSize, { RES_HD, RES_FULLHD, RES_4K }, 0);
					break;
				}
				case 2:
				{
					AVS::ComboBox_SetItems(hSize, { RES_HD, }, 0);
					break;
				}
				default:
					break;
				}
				break;
			}
			case static_cast<int>(WmMainWindowCommands::Output):
			{
				MessageOutputData* data = reinterpret_cast<MessageOutputData*>(lParam);
				if (data)
				{
					std::string text = data->text;
					delete data;

					bool isHeader = false;

					AVS::Color colorText = AVS::Color::MakeRGBA(0, 0, 0);
					if (0 == text.find("[ERROR]"))
					{
						isHeader = true;
						colorText = AVS::Color::MakeRGBA(255, 0, 0);
						text = text.substr(7);
					}
					else if (0 == text.find("[WARNING]"))
					{
						isHeader = true;
						colorText = AVS::Color::MakeRGBA(255, 0, 0);
						text = text.substr(9);
					}
					else if (0 == text.find("[SUCCESS]"))
					{
						isHeader = true;
						text = text.substr(9);
					}

					if (!isHeader)
						break;

					try
					{
						text = NSStringUtils::unescapeJson(text);
						nlohmann::json response = nlohmann::json::parse(text);

						bool isError = false;

						if (response.contains("error"))
						{
							if (response["error"].is_string())
							{
								isError = true;

								std::string error = response["error"];
								colorText = AVS::Color::MakeRGBA(255, 0, 0);

								AVS::Label_SetTextAndColor(hStatus, NSStringUtils::utf8_to_wstring(error), colorText);
							}
							else if (response["error"].is_object() && response["error"].contains("message"))
							{
								isError = true;

								std::string error = response["error"]["message"];
								colorText = AVS::Color::MakeRGBA(255, 0, 0);

								AVS::Label_SetTextAndColor(hStatus, NSStringUtils::utf8_to_wstring(error), colorText);
							}
							else if (response["error"].is_null())
							{
								isError = false;
							}
						}

						if (!isError)
						{
							if (response.value("done", false) && response.contains("response"))
							{
								std::string file_url =
									response["response"]
									["generateVideoResponse"]
									["generatedSamples"][0]
									["video"]
									["uri"]
									.get<std::string>();

								std::wstring path = plugin->m_workDirectory + L"\\" + plugin->m_engine.m_file;
								
								{
									std::wstring date_time = plugin->m_engine.GetCurrentDateTime();
									std::wstring cachePath = plugin->m_workDirectory + L"\\cache.json";
									std::string video_hash = "";
									try
									{
										video_hash = CalcFileSHA256(path);
									}
									catch (const std::exception& e)
									{
										throw std::runtime_error("SHA256 failed");
									}

									nlohmann::json cacheJson = nlohmann::json::array();
									{
										std::ifstream in(cachePath);
										if (in.is_open())
										{
											try
											{
												in >> cacheJson;
												if (!cacheJson.is_array())
													cacheJson = nlohmann::json::array();
											}
											catch (...)
											{
												cacheJson = nlohmann::json::array();
											}
										}
									}

									nlohmann::json item;

									item["video_hash"] = video_hash;
									item["uri"] = file_url;
									item["date_time"] = NSStringUtils::wstring_to_utf8(date_time);
									item["aspectRatio"] = NSStringUtils::wstring_to_utf8(plugin->m_engine.m_aspectRatio);
									item["resolution"] = NSStringUtils::wstring_to_utf8(plugin->m_engine.m_resolution);

									cacheJson.push_back(item);
									{
										std::ofstream out(cachePath, std::ios::trunc);
										if (!out.is_open())
											throw std::runtime_error("Cannot open cache.json for writing");
										out << cacheJson.dump(4);
									}
								}
							}
							else if (response.contains("name"))
							{

								if (AVS::ProgressBar_GetPos(hProgress) == 0)
								{
									AVS::ProgressBar_SetPos(hProgress, 10);
									plugin->m_engine.FakeStart();
								} else 
									AVS::ProgressBar_SetPos(hProgress, plugin->m_engine.GetFakeProgress());
							}
						}
					}
					catch (...)
					{
						AVS::Label_SetTextAndColor(hStatus, NSStringUtils::utf8_to_wstring(text), colorText);
					}
				}
				break;
			}
			case static_cast<int>(WmMainWindowCommands::OutputStop):
			{
				AVS::Button_SetSettings(hGenerate, AVS::ButtonSettings::Create(AVS::Buttons::Primary),
					CTranslate::GetInstance().GetManager()->Translate(L"Generate"));

				AVS::ProgressBar_SetPos(hProgress, 0);

				ShowWindow(hProgress, SW_HIDE);

				std::wstring path = plugin->m_workDirectory + L"\\" + plugin->m_engine.m_file;
				if (NSSystemUtils::ExistsFile(path))
				{
					if (plugin->m_callback)
					{
						plugin->m_callback(PluginId(), export_str(path.c_str()), 0, plugin->m_callbackContext);
						DestroyWindow(hwnd);
						return 0;
					}
				}

				break;
			}
			default:
				break;
			}
			break;
		}
		case WM_DESTROY:
		{
			if (plugin->m_engine.m_manager)
				plugin->m_engine.m_manager->StopAll();
			PostQuitMessage(0);
			break;
		}
		default:
			break;
		}
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	void ShowPromptWindow(CVeo3Plugin* plugin)
	{
		ACTCTX actCtx = { sizeof(ACTCTX) };
		actCtx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
		actCtx.lpResourceName = MAKEINTRESOURCE(1);
		actCtx.hModule = g_hInst;

		HANDLE hActCtx = CreateActCtx(&actCtx);
		ULONG_PTR cookie = 0;

		if (hActCtx != INVALID_HANDLE_VALUE)
			ActivateActCtx(hActCtx, &cookie);

		INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
		InitCommonControlsEx(&icex);

		const wchar_t* className = L"Veo3MainWindowClass";

		WNDCLASSEX wcCheck = { 0 };
		wcCheck.cbSize = sizeof(WNDCLASSEX);

		if (!GetClassInfoEx(GetModuleHandle(NULL), className, &wcCheck))
		{
			WNDCLASSEX wc = { 0 };
			wc.cbSize = sizeof(WNDCLASSEX);
			wc.lpfnWndProc = MainWndProc;
			wc.hInstance = GetModuleHandle(NULL);
			wc.lpszClassName = L"Veo3MainWindowClass";
			wc.hCursor = LoadCursor(NULL, IDC_ARROW);

			AVS::Color colorBack = AVS::Color::GetDefaultWindowBackground();
			HBRUSH hBackgroundBrush = CreateSolidBrush(RGB(colorBack.R, colorBack.G, colorBack.B));
			wc.hbrBackground = hBackgroundBrush;

			std::wstring iconPath = plugin->m_workDirectory + L"\\icon_internal.ico";

			HMODULE hModule = GetModuleHandle(NULL);
			wc.hIcon = (HICON)LoadImage(hModule, iconPath.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
			wc.hIconSm = (HICON)LoadImage(hModule, iconPath.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE | LR_DEFAULTCOLOR);

			RegisterClassEx(&wc);
		}

		DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		std::wstring titleWindow = CTranslate::GetInstance().GetManager()->Translate(L"Veo3");

		RECT rc = { 0, 0, 500, 410 };
		AdjustWindowRectEx(&rc, dwStyle, FALSE, 0);

		DWORD dwExStyle = WS_EX_APPWINDOW; // 0
		HWND hwnd = CreateWindowEx(dwExStyle, className, titleWindow.c_str(), dwStyle, CW_USEDEFAULT, CW_USEDEFAULT,
			rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, GetModuleHandle(NULL), plugin);

		if (plugin->m_hParentWindow)
		{
			SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, (LONG_PTR)plugin->m_hParentWindow);
			EnableWindow(plugin->m_hParentWindow, FALSE);
		}

		CenterWindow(hwnd);
		ShowWindow(hwnd, SW_SHOW);
		UpdateWindow(hwnd);

		MSG msg;
		while (GetMessage(&msg, NULL, 0, 0))
		{
			HWND hwndSettings = FindWindow(L"Veo3SettingsWindowClass", NULL);

			if (hwndSettings && IsWindow(hwndSettings))
			{
				if (IsDialogMessage(hwndSettings, &msg))
					continue;
			}

			if (!IsDialogMessage(hwnd, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}

		if (plugin->m_hParentWindow)
		{
			EnableWindow(plugin->m_hParentWindow, TRUE);
			SetForegroundWindow(plugin->m_hParentWindow);
		}

		if (hActCtx != INVALID_HANDLE_VALUE)
		{
			DeactivateActCtx(0, cookie);
			ReleaseActCtx(hActCtx);
		}
	}
} // namespace NSUI
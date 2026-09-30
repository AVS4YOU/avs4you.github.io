#include "ui.h"
#include "export_utils.h"
#include "exports.h"

#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#include "../../../sdk/ui/winapi/ui.h"
#include "../../../sdk/common/utils.h"
#include "../../../sdk/3dparty/nlohmann/json/single_include/nlohmann/json.hpp"

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

			MONITORINFO mi = {sizeof(mi)};
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

		CSoraPlugin* plugin = (CSoraPlugin*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

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

	void ShowSettingsWindow(HWND hwndParent, CSoraPlugin* plugin)
	{
		const wchar_t* className = L"Sora2SettingsWindowClass";

		WNDCLASSEX wcCheck = { 0 };
		wcCheck.cbSize = sizeof(WNDCLASSEX);

		if (!GetClassInfoEx(GetModuleHandle(NULL), className, &wcCheck))
		{
			WNDCLASSEX wc = { 0 };
			wc.cbSize = sizeof(WNDCLASSEX);

			wc.lpfnWndProc = SettingsWndProc;
			wc.hInstance = GetModuleHandle(NULL);
			wc.lpszClassName = L"Sora2SettingsWindowClass";
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

		RECT rc = {0, 0, 360, 95};
		AdjustWindowRectEx(&rc, dwStyle, FALSE, 0);

		HWND hwnd = CreateWindowEx(0, className, titleWindow.c_str(), dwStyle, CW_USEDEFAULT, CW_USEDEFAULT,
			rc.right - rc.left, rc.bottom - rc.top, hwndParent, NULL, GetModuleHandle(NULL), plugin);

		if (hwndParent)
			EnableWindow(hwndParent, FALSE);

		CenterWindow(hwnd);
		ShowWindow(hwnd, SW_SHOW);
		UpdateWindow(hwnd);
	}

	// ====================================== MAIN WINDOW ======================================
	LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		static HWND hMainEdit,
			hModel, hOrientation, hSize, hDuration,
			hProgress,
			hStatus,
			hSettings, hGenerate;

		RECT rcMain;
		GetWindowRect(hwnd, &rcMain);

		if (msg == WM_NCCREATE)
		{
			CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		CSoraPlugin* plugin = (CSoraPlugin*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

		switch (msg)
		{
		case WM_CREATE:
		{
			int SettingsButtonsW = 100;
			int WindowW = 500;

			// prompt edit
			AVS::TextEditSettings mainEditSettings = AVS::TextEditSettings::Create();
			mainEditSettings.IsMultiline = true;
			mainEditSettings.IsVscroll = true;
			hMainEdit = AVS::CreateTextEditMultiline(hwnd, GetModuleHandle(NULL), 15, 15, WindowW - 30 - 10 - SettingsButtonsW, 200, mainEditSettings);

			CTranslateManager* tr = CTranslate::GetInstance().GetManager();
			HINSTANCE hInstance = GetModuleHandle(NULL);

			int left = WindowW - 15 - SettingsButtonsW;
			int top = 15;
			int labelH = 25;
			int distance = 0;
			HWND hModelLabel = AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Model").c_str(), 
				left, top, SettingsButtonsW, labelH, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
			top += labelH;

			hModel = AVS::CreateComboBox(hwnd, (HMENU)WmMainWindowCommands::ComboModel, hInstance,
				left, top, SettingsButtonsW, 25, AVS::ComboBoxSettings::Create(),
				{ L"Sora 2", L"Sora 2 Pro" });
			top += (distance + 25);

			HWND hOrientationLabel = AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Orientation").c_str(),
				left, top, SettingsButtonsW, 25, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
			top += labelH;

			hOrientation = AVS::CreateComboBox(hwnd, (HMENU)WmMainWindowCommands::ComboOrientation, hInstance,
				left, top, SettingsButtonsW, 25, AVS::ComboBoxSettings::Create(),
				{ L"16:9", L"9:16" });
			top += (distance + 25);

			HWND hSizeLabel = AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Size").c_str(),
				left, top, SettingsButtonsW, 25, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
			top += labelH;

			hSize = AVS::CreateComboBox(hwnd, (HMENU)WmMainWindowCommands::ComboSize, hInstance,
				left, top, SettingsButtonsW, 25, AVS::ComboBoxSettings::Create(),
				{ L"1280x720" });
			top += (distance + 25);

			HWND hDurationLabel = AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Duration (s)").c_str(),
				left, top, SettingsButtonsW, 25, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
			top += labelH;

			hDuration = AVS::CreateComboBox(hwnd, (HMENU)WmMainWindowCommands::ComboDuration, hInstance,
				left, top, SettingsButtonsW, 25, AVS::ComboBoxSettings::Create(),
				{ L"4", L"8", L"12" });
			top += (distance + 25);

			hProgress = AVS::CreateProgressBar(hwnd, GetModuleHandle(NULL), 15, 230, WindowW - 30, 25, AVS::ProgressBarSettings::Create());
			ShowWindow(hProgress, SW_HIDE);

			hStatus = AVS::CreateLabel(hwnd, hInstance, L"",
				15, 275, WindowW - 215, 25, AVS::LabelSettings::Create(AVS::LabelType::Enabled));

			hSettings = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonSettings, hInstance,
				tr->Translate(L"Settings").c_str(),
				320, 275, 80, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));

			hGenerate = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonGenerate, hInstance,
				tr->Translate(L"Generate").c_str(),
				410, 275, 80, 25,
				AVS::ButtonSettings::Create(AVS::Buttons::Primary));

			// Tabs
			SetWindowPos(hMainEdit, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hModel, hMainEdit, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hOrientation, hModel, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hSize, hOrientation, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(hDuration, hSize, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
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

				std::wstring promptValue = L"";

				int promptLen = GetWindowTextLength(hMainEdit);
				if (promptLen > 0)
				{
					std::wstring prompt(promptLen, L'\0');
					GetWindowText(hMainEdit, &prompt[0], promptLen + 1);
					promptValue = std::move(prompt);
				}

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

				AVS::Button_SetSettings(hGenerate, AVS::ButtonSettings::Create(AVS::Buttons::Default),
					CTranslate::GetInstance().GetManager()->Translate(L"Cancel"));

				AVS::Label_SetText(hStatus, L"");
				ShowWindow(hProgress, SW_SHOW);

				auto swapResolution = [](const std::wstring& resolution) -> std::wstring
				{
					size_t pos = resolution.find(L'x');
					if (pos == std::wstring::npos)
						return resolution;

					std::wstring width = resolution.substr(0, pos);
					std::wstring height = resolution.substr(pos + 1);

					return height + L"x" + width;
				};

				std::wstring size = AVS::ComboBox_GetCurrentText(hSize);
				std::wstring orientation = AVS::ComboBox_GetCurrentText(hOrientation);
				if (orientation == L"9:16")
					size = swapResolution(size);

				plugin->m_engine.m_resolution = size;
				plugin->m_engine.m_seconds = AVS::ComboBox_GetCurrentText(hDuration);

				int model = AVS::ComboBox_GetCurrent(hModel);
				if (model == 1)
					plugin->m_engine.m_model = L"sora-2-pro";
				else
					plugin->m_engine.m_model = L"sora-2";

				plugin->m_engine.Process(plugin, plugin->m_workDirectory);
				break;
			}
			case static_cast<int>(WmMainWindowCommands::ButtonSettings):
			{
				ShowSettingsWindow(hwnd, plugin);
				break;
			}
			case static_cast<int>(WmMainWindowCommands::ComboModel):
			{
				int nModel = AVS::ComboBox_GetCurrent(hModel);
				switch (nModel)
				{
				case 0:
				{
					AVS::ComboBox_SetItems(hSize, { L"1280x720" }, 0);
					break;
				}
				case 1:
				{
					AVS::ComboBox_SetItems(hSize, { L"1280x720", L"1792x1024" }, 0);
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
							if (response.contains("status") && response["status"].is_string() &&
								response.contains("id") && response["id"].is_string())
							{
								std::string status = response["status"];
								std::string id = response["id"];

								int progress = 0;
								if (response.contains("progress") && response["progress"].is_number())
									progress = (int)response["progress"];

								if (progress == 10)
								{
									plugin->m_engine.FakeStart();
									AVS::ProgressBar_SetPos(hProgress, (int)plugin->m_engine.GetFakeProgress());
								}
								else
								{
									AVS::ProgressBar_SetPos(hProgress, progress);
								}

								AVS::Label_SetTextAndColor(hStatus, NSStringUtils::utf8_to_wstring("ID: " + id), colorText);
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

	void ShowPromptWindow(CSoraPlugin* plugin)
	{
		ACTCTX actCtx = {sizeof(ACTCTX)};
		actCtx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
		actCtx.lpResourceName = MAKEINTRESOURCE(1);
		actCtx.hModule = g_hInst;

		HANDLE hActCtx = CreateActCtx(&actCtx);
		ULONG_PTR cookie = 0;

		if (hActCtx != INVALID_HANDLE_VALUE)
			ActivateActCtx(hActCtx, &cookie);

		INITCOMMONCONTROLSEX icex = {sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
		InitCommonControlsEx(&icex);

		const wchar_t* className = L"Sora2MainWindowClass";

		WNDCLASSEX wcCheck = { 0 };
		wcCheck.cbSize = sizeof(WNDCLASSEX);

		if (!GetClassInfoEx(GetModuleHandle(NULL), className, &wcCheck))
		{
			WNDCLASSEX wc = { 0 };
			wc.cbSize = sizeof(WNDCLASSEX);
			wc.lpfnWndProc = MainWndProc;
			wc.hInstance = GetModuleHandle(NULL);
			wc.lpszClassName = L"Sora2MainWindowClass";
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
		std::wstring titleWindow = CTranslate::GetInstance().GetManager()->Translate(L"Sora2");

		RECT rc = {0, 0, 500, 310};
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
			HWND hwndSettings = FindWindow(L"Sora2SettingsWindowClass", NULL);

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
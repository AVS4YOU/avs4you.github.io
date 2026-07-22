#include "nano-banana_ui.h"
#include "export_utils.h"
#include "exports.h"
#include "sha256.h"

#include <commctrl.h>
#include <fstream>
#pragma comment(lib, "comctl32.lib")

#include "../../../sdk/ui/winapi/ui.h"
#include "../../../sdk/common/utils.h"
#include "../../../sdk/3dparty/nlohmann/json/single_include/nlohmann/json.hpp"

#define NANO_BANANA_SETTINGS_WINDOW_CLASS L"NanoBananaSettingsWindowClass"
#define NANO_BANANA_MAIN_WINDOW_CLASS L"NanoBananaMainWindowClass"

#define MODEL_FLASH_LITE_NAME L"Nano Banana 2 Lite"
#define MODEL_FLASH_NAME L"Nano Banana 2"
#define MODEL_PRO_NAME L"Nano Banana Pro"
#define MODEL_LEGACY_NAME L"Nano Banana"

#define MODEL_FLASH_LITE L"gemini-3.1-flash-lite-image"
#define MODEL_FLASH L"gemini-3.1-flash-image"
#define MODEL_PRO L"gemini-3-pro-image"
#define MODEL_LEGACY L"gemini-2.5-flash-image"

static const int kModeFirst = static_cast<int>(WmMainWindowCommands::ButtonTextToImage);
static const int kModeLast = static_cast<int>(WmMainWindowCommands::ButtonVideoToImage);
static int activeNow = static_cast<int>(WmMainWindowCommands::ButtonTextToImage);

namespace NSUI
{
	void CenterWindow(HWND hwnd)
	{
		RECT rcWin;
		GetWindowRect(hwnd, &rcWin);

		RECT rcWork;
		HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		if (GetMonitorInfo(hMonitor, &mi))
			rcWork = mi.rcWork;
		else
			SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);

		int winWidth = rcWin.right - rcWin.left;
		int winHeight = rcWin.bottom - rcWin.top;
		int x = rcWork.left + (rcWork.right - rcWork.left - winWidth) / 2;
		int y = rcWork.top + (rcWork.bottom - rcWork.top - winHeight) / 2;

		SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	}

	LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		static HWND hEdit, hBtnOK, hBtnCancel, hBtnDelete;

		if (msg == WM_NCCREATE)
		{
			CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		CNanoBananaPlugin* plugin = (CNanoBananaPlugin*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

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
				SetWindowText(hEdit, keyValue.c_str());

			CTranslateManager* tr = CTranslate::GetInstance().GetManager();
			hBtnOK = AVS::CreateButton(hwnd, (HMENU)0x8001, GetModuleHandle(NULL), tr->Translate(L"Save").c_str(), 265, 55, 80, 25, AVS::ButtonSettings::Create(AVS::Buttons::Primary));
			hBtnCancel = AVS::CreateButton(hwnd, (HMENU)0x8002, GetModuleHandle(NULL), tr->Translate(L"Cancel").c_str(), 175, 55, 80, 25, AVS::ButtonSettings::Create(AVS::Buttons::Default));
			hBtnDelete = AVS::CreateButton(hwnd, (HMENU)0x8003, GetModuleHandle(NULL), tr->Translate(L"Delete API-key").c_str(), 15, 55, 120, 25, AVS::ButtonSettings::Create(AVS::Buttons::Default));

			SetFocus(hEdit);
			break;
		}
		case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
			case 0x8001:
			{
				int len = GetWindowTextLength(hEdit);
				if (len > 0)
				{
					std::wstring key(len, L'\0');
					GetWindowText(hEdit, &key[0], len + 1);
					NSSystemUtils::WriteWStringToUtf8File(key, plugin->m_workDirectory + L"\\app.key", false);
				}
				closeWindow();
				break;
			}
			case IDCANCEL:
			case 0x8002:
				closeWindow();
				break;
			case 0x8003:
				NSSystemUtils::RemoveFile(plugin->m_workDirectory + L"\\app.key");
				SetWindowText(hEdit, L"");
				break;
			default:
				break;
			}
			break;
		}
		case WM_CLOSE:
			closeWindow();
			return 0;
		default:
			break;
		}
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	void ShowSettingsWindow(HWND hwndParent, CNanoBananaPlugin* plugin)
	{
		const wchar_t* className = NANO_BANANA_SETTINGS_WINDOW_CLASS;

		WNDCLASSEX wcCheck = { 0 };
		wcCheck.cbSize = sizeof(WNDCLASSEX);

		if (!GetClassInfoEx(GetModuleHandle(NULL), className, &wcCheck))
		{
			WNDCLASSEX wc = { 0 };
			wc.cbSize = sizeof(WNDCLASSEX);
			wc.lpfnWndProc = SettingsWndProc;
			wc.hInstance = GetModuleHandle(NULL);
			wc.lpszClassName = className;
			wc.hCursor = LoadCursor(NULL, IDC_ARROW);

			AVS::Color colorBack = AVS::Color::GetDefaultWindowBackground();
			wc.hbrBackground = CreateSolidBrush(RGB(colorBack.R, colorBack.G, colorBack.B));
			std::wstring iconPath = plugin->m_workDirectory + L"\\icon_internal.ico";
			wc.hIcon = (HICON)LoadImage(GetModuleHandle(NULL), iconPath.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
			wc.hIconSm = (HICON)LoadImage(GetModuleHandle(NULL), iconPath.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
			RegisterClassEx(&wc);
		}

		DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
		std::wstring titleWindow = CTranslate::GetInstance().GetManager()->Translate(L"Settings");

		RECT rc = { 0, 0, 360, 95 };
		AdjustWindowRectEx(&rc, dwStyle, FALSE, 0);
		HWND hwnd = CreateWindowEx(0, className, titleWindow.c_str(), dwStyle, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, hwndParent, NULL, GetModuleHandle(NULL), plugin);

		if (hwndParent)
			EnableWindow(hwndParent, FALSE);

		CenterWindow(hwnd);
		ShowWindow(hwnd, SW_SHOW);
		UpdateWindow(hwnd);
	}

	static std::wstring GetTextFromEdit(HWND edit)
	{
		int len = GetWindowTextLength(edit);
		if (len <= 0)
			return L"";

		std::wstring value(len, L'\0');
		GetWindowText(edit, &value[0], len + 1);
		return value;
	}

	static void SetModeButtons(const std::vector<HWND>& buttons, int selected)
	{
		CTranslateManager* tr = CTranslate::GetInstance().GetManager();
		std::vector<std::wstring> names = {
			tr->Translate(L"Text"),
			tr->Translate(L"Edit"),
			tr->Translate(L"Multi-turn"),
			tr->Translate(L"Search"),
			tr->Translate(L"Image Search"),
			tr->Translate(L"Video")
		};

		for (int i = 0; i < (int)buttons.size(); i++)
		{
			AVS::Button_SetSettings(buttons[i], AVS::ButtonSettings::Create((kModeFirst + i) == selected ? AVS::Buttons::ToggleGroupEnable : AVS::Buttons::ToggleGroupDisable), names[i]);
		}
	}

	static std::wstring GetModeDescription(int mode)
	{
		if (mode == static_cast<int>(WmMainWindowCommands::ButtonImageEdit))
			return L"Provide an image and use text prompts to add, remove, or modify elements, change the style, or adjust the color grading.";
		if (mode == static_cast<int>(WmMainWindowCommands::ButtonMultiTurn))
			return L"Make changes and improvements to previously generated images.";
		if (mode == static_cast<int>(WmMainWindowCommands::ButtonGoogleSearch))
			return L"Use the Google Search tool to generate images based on real-time information, such as weather forecasts, stock charts, or recent events.";
		if (mode == static_cast<int>(WmMainWindowCommands::ButtonImageSearch))
			return L"Grounding with Google Image Search allows models to use web images retrieved via Google Image Search as visual context for image generation.";
		if (mode == static_cast<int>(WmMainWindowCommands::ButtonVideoToImage))
			return L"Video-to-image generation allows you to generate new images using a video's context as a multimodal reference. This is useful for creating video thumbnails, cinematic posters, summary infographics, or artwork inspired by a video scene.";
		return L"Image generation (text-to-image)";
	}

	static std::vector<std::wstring> WrapModeDescription(const std::wstring& text)
	{
		const size_t maxLineLength = 76;
		const size_t maxLines = 4;
		std::vector<std::wstring> lines;
		std::wstring current;
		size_t start = 0;

		while (start < text.size() && lines.size() < maxLines)
		{
			size_t end = text.find(L' ', start);
			std::wstring word = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
			if (!current.empty() && current.size() + 1 + word.size() > maxLineLength)
			{
				lines.push_back(current);
				current.clear();
				if (lines.size() == maxLines)
					break;
			}
			if (!current.empty())
				current += L' ';
			current += word;
			if (end == std::wstring::npos)
				break;
			start = end + 1;
		}

		if (!current.empty() && lines.size() < maxLines)
			lines.push_back(current);
		while (lines.size() < maxLines)
			lines.push_back(L"");
		return lines;
	}

	static void UpdateModeDescriptionLabels(HWND labels[4], const std::wstring& text)
	{
		std::vector<std::wstring> lines = WrapModeDescription(text);
		for (int i = 0; i < 4; i++)
			AVS::Label_SetText(labels[i], lines[i]);
	}

	static bool IsReferenceMode()
	{
		return activeNow == static_cast<int>(WmMainWindowCommands::ButtonImageEdit) ||
			activeNow == static_cast<int>(WmMainWindowCommands::ButtonMultiTurn);
	}

	static std::wstring GetCachePath(CNanoBananaPlugin* plugin)
	{
		return plugin->m_workDirectory + L"\\cache.json";
	}

	static bool FindInteractionIdInCache(CNanoBananaPlugin* plugin, const std::string& imageHash, std::wstring& interactionId)
	{
		std::wstring cachePath = GetCachePath(plugin);
		DWORD attr = GetFileAttributesW(cachePath.c_str());
		if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
			return false;

		try
		{
			nlohmann::json cacheJson;
			std::ifstream in(cachePath);
			if (!in.is_open())
				return false;
			in >> cacheJson;

			if (cacheJson.is_object() && cacheJson.contains(imageHash) && cacheJson[imageHash].is_string())
			{
				interactionId = NSStringUtils::utf8_to_wstring(cacheJson[imageHash].get<std::string>());
				return true;
			}
		}
		catch (...)
		{
			return false;
		}

		return false;
	}

	static void SaveInteractionIdToCache(CNanoBananaPlugin* plugin, const std::wstring& imagePath, const std::wstring& interactionId)
	{
		if (interactionId.empty())
			return;

		try
		{
			std::string imageHash = CalcFileSHA256(imagePath);
			std::wstring cachePath = GetCachePath(plugin);
			nlohmann::json cacheJson = nlohmann::json::object();

			std::ifstream in(cachePath);
			if (in.is_open())
			{
				try
				{
					in >> cacheJson;
					if (!cacheJson.is_object())
						cacheJson = nlohmann::json::object();
				}
				catch (...)
				{
					cacheJson = nlohmann::json::object();
				}
			}

			cacheJson[imageHash] = NSStringUtils::wstring_to_utf8(interactionId);

			std::ofstream out(cachePath, std::ios::trunc);
			if (out.is_open())
				out << cacheJson.dump(4);
		}
		catch (...)
		{
		}
	}

	static bool CompleteGeneration(HWND hwnd, CNanoBananaPlugin* plugin)
	{
		std::wstring path = plugin->m_workDirectory + L"\\" + plugin->m_engine.m_file;
		if (!NSSystemUtils::ExistsFile(path) || !plugin->m_callback)
			return false;

		plugin->m_callback(export_str(L"NanoBanana.plugin"), export_str(path.c_str()), 0, plugin->m_callbackContext);
		DestroyWindow(hwnd);
		return true;
	}

	LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		static HWND hMainEdit, hVideoUrlEdit, hVideoUrlLabel;
		static HWND hModel, hAspectRatio, hImageSize;
		static HWND hProgress, hStatus, hSettings, hGenerate;
		static HWND hTextToImage, hImageEdit, hMultiTurn, hGoogleSearch, hImageSearch, hVideoToImage;
		static HWND hFile[6], hPathFile[6], hModeDescription[4];
		static std::wstring editPathText[6];
		static std::wstring multiTurnPathText;

		if (msg == WM_NCCREATE)
		{
			CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		CNanoBananaPlugin* plugin = (CNanoBananaPlugin*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

		switch (msg)
		{
		case WM_CREATE:
		{
			int windowW = 675;
			int settingsW = 130;
			int promptW = 500;
			CTranslateManager* tr = CTranslate::GetInstance().GetManager();
			HINSTANCE hInstance = GetModuleHandle(NULL);

			hTextToImage = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonTextToImage, hInstance, tr->Translate(L"Text").c_str(), 15, 15, 70, 25, AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupEnable));
			hImageEdit = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonImageEdit, hInstance, tr->Translate(L"Edit").c_str(), 85, 15, 70, 25, AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable));
			hMultiTurn = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonMultiTurn, hInstance, tr->Translate(L"Multi-turn").c_str(), 155, 15, 90, 25, AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable));
			hGoogleSearch = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonGoogleSearch, hInstance, tr->Translate(L"Search").c_str(), 245, 15, 70, 25, AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable));
			hImageSearch = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonImageSearch, hInstance, tr->Translate(L"Image Search").c_str(), 315, 15, 110, 25, AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable));
			hVideoToImage = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonVideoToImage, hInstance, tr->Translate(L"Video").c_str(), 425, 15, 90, 25, AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable));

			AVS::TextEditSettings mainEditSettings = AVS::TextEditSettings::Create();
			mainEditSettings.IsMultiline = true;
			mainEditSettings.IsVscroll = true;
			hMainEdit = AVS::CreateTextEditMultiline(hwnd, hInstance, 15, 45, promptW, 190, mainEditSettings);
			for (int i = 0; i < 4; i++)
				hModeDescription[i] = AVS::CreateLabel(hwnd, hInstance, L"", 15, 240 + i * 18, promptW, 18, AVS::LabelSettings::Create(AVS::LabelType::Enabled, DT_LEFT));
			UpdateModeDescriptionLabels(hModeDescription, GetModeDescription(activeNow));

			int left = windowW - 15 - settingsW;
			int top = 45;
			int labelH = 20;

			AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Model").c_str(), left, top, settingsW, labelH, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
			top += labelH;
			hModel = AVS::CreateComboBox(hwnd, (HMENU)WmMainWindowCommands::ComboModel, hInstance, left, top, settingsW, 25, AVS::ComboBoxSettings::Create(), { MODEL_FLASH_NAME, MODEL_PRO_NAME, MODEL_FLASH_LITE_NAME, MODEL_LEGACY_NAME });
			top += 30;

			AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Aspect").c_str(), left, top, settingsW, labelH, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
			top += labelH;
			hAspectRatio = AVS::CreateComboBox(hwnd, (HMENU)WmMainWindowCommands::ComboAspectRatio, hInstance, left, top, settingsW, 25, AVS::ComboBoxSettings::Create(), { L"16:9", L"9:16", L"21:9", L"4:5", L"5:4", L"4:3", L"3:4", L"3:2", L"2:3", L"1:1" });
			top += 30;

			AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"Size").c_str(), left, top, settingsW, labelH, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
			top += labelH;
			hImageSize = AVS::CreateComboBox(hwnd, (HMENU)WmMainWindowCommands::ComboImageSize, hInstance, left, top, settingsW, 25, AVS::ComboBoxSettings::Create(), { L"1K", L"2K", L"4K", L"512", L"Default" });
			top += 30;

			hVideoUrlLabel = AVS::CreateLabel(hwnd, hInstance, tr->Translate(L"YouTube URL").c_str(), 15, 315, 90, 25, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
			hVideoUrlEdit = AVS::CreateTextEditMultiline(hwnd, hInstance, 100, 315, windowW - 115, 25, AVS::TextEditSettings::Create());
			ShowWindow(hVideoUrlLabel, SW_HIDE);
			ShowWindow(hVideoUrlEdit, SW_HIDE);

			for (int i = 0; i < 6; i++)
			{
				int y = 315 + i * 30;
				std::wstring referenceText = tr->Translate(L"Reference image") + L" #" + std::to_wstring(i + 1);
				editPathText[i] = referenceText;
				hFile[i] = AVS::CreateButton(hwnd, (HMENU)(static_cast<int>(WmMainWindowCommands::ButtonFile1) + i), hInstance, tr->Translate(L"Add File").c_str(), 15, y, 80, 25, AVS::ButtonSettings::Create(AVS::Buttons::Default));
				hPathFile[i] = AVS::CreateLabel(hwnd, hInstance, referenceText.c_str(), 100, y, windowW - 115, 25, AVS::LabelSettings::Create(AVS::LabelType::Enabled));
				ShowWindow(hFile[i], SW_HIDE);
				ShowWindow(hPathFile[i], SW_HIDE);
			}
			multiTurnPathText = tr->Translate(L"Image from a previous generation");

			hProgress = AVS::CreateProgressBar(hwnd, hInstance, 15, 505, windowW - 30, 25, AVS::ProgressBarSettings::Create());
			ShowWindow(hProgress, SW_HIDE);
			hStatus = AVS::CreateLabel(hwnd, hInstance, L"", 15, 535, windowW - 215, 25, AVS::LabelSettings::Create(AVS::LabelType::Enabled));
			hSettings = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonSettings, hInstance, tr->Translate(L"Settings").c_str(), windowW - 180, 535, 80, 25, AVS::ButtonSettings::Create(AVS::Buttons::Default));
			hGenerate = AVS::CreateButton(hwnd, (HMENU)WmMainWindowCommands::ButtonGenerate, hInstance, tr->Translate(L"Generate").c_str(), windowW - 90, 535, 75, 25, AVS::ButtonSettings::Create(AVS::Buttons::Primary));

			plugin->m_hWindow = hwnd;
			break;
		}
		case WM_COMMAND:
		{
			int command = static_cast<int>(LOWORD(wParam));
			if (command >= kModeFirst && command <= kModeLast)
			{
				activeNow = command;
				SetModeButtons({ hTextToImage, hImageEdit, hMultiTurn, hGoogleSearch, hImageSearch, hVideoToImage }, activeNow);
			UpdateModeDescriptionLabels(hModeDescription, GetModeDescription(activeNow));

				bool showEditRefs = activeNow == static_cast<int>(WmMainWindowCommands::ButtonImageEdit);
				bool showMultiTurnRef = activeNow == static_cast<int>(WmMainWindowCommands::ButtonMultiTurn);
				bool showVideo = activeNow == static_cast<int>(WmMainWindowCommands::ButtonVideoToImage);
				for (int i = 0; i < 6; i++)
				{
					bool showFile = showEditRefs || (showMultiTurnRef && i == 0);
					if (showEditRefs)
						AVS::Label_SetText(hPathFile[i], editPathText[i]);
					else if (showMultiTurnRef && i == 0)
						AVS::Label_SetText(hPathFile[i], multiTurnPathText);
					ShowWindow(hFile[i], showFile ? SW_SHOW : SW_HIDE);
					ShowWindow(hPathFile[i], showFile ? SW_SHOW : SW_HIDE);
				}
				ShowWindow(hVideoUrlLabel, showVideo ? SW_SHOW : SW_HIDE);
				ShowWindow(hVideoUrlEdit, showVideo ? SW_SHOW : SW_HIDE);

				if (activeNow == static_cast<int>(WmMainWindowCommands::ButtonGoogleSearch))
				{
					AVS::ComboBox_SetItems(hModel, { MODEL_FLASH_NAME, MODEL_PRO_NAME }, 0);
					AVS::ComboBox_SetItems(hImageSize, { L"1K", L"2K", L"4K", L"Default" }, 0);
				}
				else if (activeNow == static_cast<int>(WmMainWindowCommands::ButtonImageSearch) ||
					activeNow == static_cast<int>(WmMainWindowCommands::ButtonVideoToImage))
				{
					AVS::ComboBox_SetItems(hModel, { MODEL_FLASH_NAME }, 0);
				}
				else
				{
					AVS::ComboBox_SetItems(hModel, { MODEL_FLASH_NAME, MODEL_PRO_NAME, MODEL_FLASH_LITE_NAME, MODEL_LEGACY_NAME }, 0);
				}
				break;
			}

			switch (LOWORD(wParam))
			{
			case static_cast<int>(WmMainWindowCommands::ButtonGenerate):
			{
				if (plugin->m_engine.m_manager && 0 < plugin->m_engine.m_manager->GetTasksCount())
				{
					plugin->m_engine.m_manager->StopAll();
					AVS::Button_SetSettings(hGenerate, AVS::ButtonSettings::Create(AVS::Buttons::Primary), CTranslate::GetInstance().GetManager()->Translate(L"Generate"));
					ShowWindow(hProgress, SW_HIDE);
					break;
				}

				std::wstring promptValue = GetTextFromEdit(hMainEdit);
				if (promptValue.empty())
				{
					AVS::Label_SetTextAndColor(hStatus, CTranslate::GetInstance().GetManager()->Translate(L"Prompt required"), AVS::Color::MakeRGBA(255, 0, 0));
					break;
				}

				std::wstring keyValue = NSSystemUtils::ReadWStringFromUtf8File(plugin->m_workDirectory + L"\\app.key");
				if (keyValue.empty())
				{
					ShowSettingsWindow(hwnd, plugin);
					break;
				}

				plugin->m_engine.m_prompt = promptValue;
				plugin->m_engine.m_key = keyValue;
				plugin->m_engine.m_aspectRatio = AVS::ComboBox_GetCurrentText(hAspectRatio);
				plugin->m_engine.m_imageSize = AVS::ComboBox_GetCurrentText(hImageSize);
				plugin->m_engine.m_mimeType = L"image/jpeg";
				plugin->m_engine.m_additional_files_paths.clear();
				plugin->m_engine.m_videoUri = GetTextFromEdit(hVideoUrlEdit);

				std::wstring modelName = AVS::ComboBox_GetCurrentText(hModel);
				if (modelName == MODEL_PRO_NAME)
					plugin->m_engine.m_model = MODEL_PRO;
				else if (modelName == MODEL_FLASH_LITE_NAME)
					plugin->m_engine.m_model = MODEL_FLASH_LITE;
				else if (modelName == MODEL_LEGACY_NAME)
					plugin->m_engine.m_model = MODEL_LEGACY;
				else
					plugin->m_engine.m_model = MODEL_FLASH;

				plugin->m_engine.m_generation_mode = NSGenerationMode::TextToImage;
				if (activeNow == static_cast<int>(WmMainWindowCommands::ButtonImageEdit))
				{
					for (int i = 0; i < 6; i++)
					{
						std::wstring value = AVS::Label_GetText(hPathFile[i]);
						if (NSSystemUtils::ExistsFile(value))
							plugin->m_engine.m_additional_files_paths.push_back(value);
					}

					if (plugin->m_engine.m_additional_files_paths.empty())
					{
						AVS::Label_SetTextAndColor(hStatus, CTranslate::GetInstance().GetManager()->Translate(L"Required input file. At least one image."), AVS::Color::MakeRGBA(255, 0, 0));
						break;
					}
					plugin->m_engine.m_generation_mode = NSGenerationMode::ImageEdit;
				}
				else if (activeNow == static_cast<int>(WmMainWindowCommands::ButtonMultiTurn))
				{
					std::wstring file = AVS::Label_GetText(hPathFile[0]);
					if (!NSSystemUtils::ExistsFile(file))
					{
						AVS::Label_SetTextAndColor(hStatus, CTranslate::GetInstance().GetManager()->Translate(L"Required input file. At least one image."), AVS::Color::MakeRGBA(255, 0, 0));
						break;
					}

					std::wstring interactionId;
					try
					{
						std::string imageHash = CalcFileSHA256(file);
						if (!FindInteractionIdInCache(plugin, imageHash, interactionId))
						{
							AVS::Label_SetTextAndColor(hStatus, CTranslate::GetInstance().GetManager()->Translate(L"Could not find cache for this file."), AVS::Color::MakeRGBA(255, 0, 0));
							break;
						}
					}
					catch (const std::exception& e)
					{
						AVS::Label_SetTextAndColor(hStatus, NSStringUtils::utf8_to_wstring(e.what()), AVS::Color::MakeRGBA(255, 0, 0));
						break;
					}
					catch (...)
					{
						AVS::Label_SetTextAndColor(hStatus, NSStringUtils::utf8_to_wstring("[cache.json] Unknown exception"), AVS::Color::MakeRGBA(255, 0, 0));
						break;
					}

					plugin->m_engine.m_previousInteractionId = interactionId;
					plugin->m_engine.m_generation_mode = NSGenerationMode::MultiTurnEdit;
				}
				else if (activeNow == static_cast<int>(WmMainWindowCommands::ButtonGoogleSearch))
				{
					plugin->m_engine.m_generation_mode = NSGenerationMode::GoogleSearch;
				}
				else if (activeNow == static_cast<int>(WmMainWindowCommands::ButtonImageSearch))
				{
					plugin->m_engine.m_generation_mode = NSGenerationMode::ImageSearch;
					plugin->m_engine.m_model = MODEL_FLASH;
				}
				else if (activeNow == static_cast<int>(WmMainWindowCommands::ButtonVideoToImage))
				{
					plugin->m_engine.m_generation_mode = NSGenerationMode::VideoToImage;
					plugin->m_engine.m_model = MODEL_FLASH;
					if (plugin->m_engine.m_videoUri.empty())
					{
						AVS::Label_SetTextAndColor(hStatus, CTranslate::GetInstance().GetManager()->Translate(L"YouTube URL required"), AVS::Color::MakeRGBA(255, 0, 0));
						break;
					}
				}

				AVS::Button_SetSettings(hGenerate, AVS::ButtonSettings::Create(AVS::Buttons::Default), CTranslate::GetInstance().GetManager()->Translate(L"Cancel"));
				AVS::Label_SetText(hStatus, L"");
				AVS::ProgressBar_SetPos(hProgress, 10);
				plugin->m_engine.FakeStart();
				ShowWindow(hProgress, SW_SHOW);
				plugin->m_engine.Process(plugin, plugin->m_workDirectory);
				break;
			}
			case static_cast<int>(WmMainWindowCommands::ButtonSettings):
				ShowSettingsWindow(hwnd, plugin);
				break;
			case static_cast<int>(WmMainWindowCommands::ButtonFile1):
			case static_cast<int>(WmMainWindowCommands::ButtonFile2):
			case static_cast<int>(WmMainWindowCommands::ButtonFile3):
			case static_cast<int>(WmMainWindowCommands::ButtonFile4):
			case static_cast<int>(WmMainWindowCommands::ButtonFile5):
			case static_cast<int>(WmMainWindowCommands::ButtonFile6):
			{
				wchar_t path[MAX_PATH] = {};
				OPENFILENAME ofn = {};
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = hwnd;
				ofn.lpstrFile = path;
				ofn.nMaxFile = MAX_PATH;
				ofn.lpstrFilter = L"Image files (*.png;*.jpg;*.jpeg;*.webp)\0*.png;*.jpg;*.jpeg;*.webp\0All files (*.*)\0*.*\0";
				ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

				if (GetOpenFileNameW(&ofn))
				{
					int index = command - static_cast<int>(WmMainWindowCommands::ButtonFile1);
					if (activeNow == static_cast<int>(WmMainWindowCommands::ButtonMultiTurn) && index == 0)
					{
						multiTurnPathText = path;
						AVS::Label_SetText(hPathFile[index], multiTurnPathText);
					}
					else if (activeNow == static_cast<int>(WmMainWindowCommands::ButtonImageEdit))
					{
						editPathText[index] = path;
						AVS::Label_SetText(hPathFile[index], editPathText[index]);
					}
				}
				break;
			}
			case static_cast<int>(WmMainWindowCommands::ComboModel):
			{
				std::wstring modelName = AVS::ComboBox_GetCurrentText(hModel);
				if (activeNow == static_cast<int>(WmMainWindowCommands::ButtonGoogleSearch))
					AVS::ComboBox_SetItems(hImageSize, { L"1K", L"2K", L"4K", L"Default" }, 0);
				else if (modelName == MODEL_FLASH_LITE_NAME)
					AVS::ComboBox_SetItems(hImageSize, { L"1K" }, 0);
				else if (modelName == MODEL_FLASH_NAME)
					AVS::ComboBox_SetItems(hImageSize, { L"512", L"1K", L"2K", L"4K", L"Default" }, 1);
				else
					AVS::ComboBox_SetItems(hImageSize, { L"1K", L"2K", L"4K", L"Default" }, 0);
				break;
			}
			case static_cast<int>(WmMainWindowCommands::Output):
			{
				MessageOutputData* data = reinterpret_cast<MessageOutputData*>(lParam);
				if (!data)
					break;

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

					if (response.contains("error"))
					{
						if (response["error"].is_object() && response["error"].contains("message"))
							text = response["error"]["message"].get<std::string>();
						else if (response["error"].is_string())
							text = response["error"].get<std::string>();
						AVS::Label_SetTextAndColor(hStatus, NSStringUtils::utf8_to_wstring(text), AVS::Color::MakeRGBA(255, 0, 0));
						break;
					}

					if (response.contains("interactionId") && response["interactionId"].is_string())
					{
						plugin->m_engine.m_previousInteractionId = NSStringUtils::utf8_to_wstring(response["interactionId"].get<std::string>());
						std::wstring path = plugin->m_workDirectory + L"\\" + plugin->m_engine.m_file;
						SaveInteractionIdToCache(plugin, path, plugin->m_engine.m_previousInteractionId);
						AVS::ProgressBar_SetPos(hProgress, 100);
						AVS::Label_SetText(hStatus, CTranslate::GetInstance().GetManager()->Translate(L"Done"));
						if (CompleteGeneration(hwnd, plugin))
							return 0;
					}
					else if (response.contains("id") && response["id"].is_string())
					{
						AVS::ProgressBar_SetPos(hProgress, plugin->m_engine.GetFakeProgress());
					}
				}
				catch (...)
				{
					AVS::Label_SetTextAndColor(hStatus, NSStringUtils::utf8_to_wstring(text), colorText);
				}
				break;
			}
			case static_cast<int>(WmMainWindowCommands::OutputStop):
			{
				AVS::Button_SetSettings(hGenerate, AVS::ButtonSettings::Create(AVS::Buttons::Primary), CTranslate::GetInstance().GetManager()->Translate(L"Generate"));
				AVS::ProgressBar_SetPos(hProgress, 0);
				ShowWindow(hProgress, SW_HIDE);

				if (CompleteGeneration(hwnd, plugin))
					return 0;
				break;
			}
			default:
				break;
			}
			break;
		}
		case WM_DESTROY:
			if (plugin->m_engine.m_manager)
				plugin->m_engine.m_manager->StopAll();
			PostQuitMessage(0);
			break;
		default:
			break;
		}
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	void ShowPromptWindow(CNanoBananaPlugin* plugin)
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

		const wchar_t* className = NANO_BANANA_MAIN_WINDOW_CLASS;
		WNDCLASSEX wcCheck = { 0 };
		wcCheck.cbSize = sizeof(WNDCLASSEX);

		if (!GetClassInfoEx(GetModuleHandle(NULL), className, &wcCheck))
		{
			WNDCLASSEX wc = { 0 };
			wc.cbSize = sizeof(WNDCLASSEX);
			wc.lpfnWndProc = MainWndProc;
			wc.hInstance = GetModuleHandle(NULL);
			wc.lpszClassName = className;
			wc.hCursor = LoadCursor(NULL, IDC_ARROW);

			AVS::Color colorBack = AVS::Color::GetDefaultWindowBackground();
			wc.hbrBackground = CreateSolidBrush(RGB(colorBack.R, colorBack.G, colorBack.B));
			std::wstring iconPath = plugin->m_workDirectory + L"\\icon_internal.ico";
			wc.hIcon = (HICON)LoadImage(GetModuleHandle(NULL), iconPath.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
			wc.hIconSm = (HICON)LoadImage(GetModuleHandle(NULL), iconPath.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
			RegisterClassEx(&wc);
		}

		DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		std::wstring titleWindow = CTranslate::GetInstance().GetManager()->Translate(L"Nano Banana");

		RECT rc = { 0, 0, 675, 575 };
		AdjustWindowRectEx(&rc, dwStyle, FALSE, 0);
		HWND hwnd = CreateWindowEx(WS_EX_APPWINDOW, className, titleWindow.c_str(), dwStyle, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, GetModuleHandle(NULL), plugin);

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
			HWND hwndSettings = FindWindow(NANO_BANANA_SETTINGS_WINDOW_CLASS, NULL);
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

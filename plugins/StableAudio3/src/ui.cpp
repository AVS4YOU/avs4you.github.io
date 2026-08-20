#include "ui.h"
#include "../../../sdk/ui/winapi/ui.h"
#include "export_utils.h"
#include "sa3_backend.h"
#include "system_info.h"

#include <cmath>
#include <commctrl.h>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

extern HMODULE g_module;

namespace {
	constexpr int ID_MODEL = 103;
	constexpr int ID_ENCODING = 104;
	constexpr int ID_DEVICE = 105;
	constexpr int ID_DURATION = 106;
	constexpr int ID_DOWNLOAD = 114;
	constexpr int ID_ACTION = 115;
	constexpr int ID_SETTINGS = 116;
	constexpr int ID_SETTINGS_SAVE = 201;
	constexpr int ID_SETTINGS_CANCEL = 202;
	constexpr int ID_SETTINGS_KEEP = 203;
	constexpr int CLIENT_WIDTH = 760;
	constexpr int CLIENT_HEIGHT = 600;

	struct GenerationSettings {
		int steps = 8;
		long long seed = -1;
		float cfg = 1.0f;
		int threads = 0;
		float padding = 6.0f;
		std::wstring shift = L"LogSNR";
		bool keepModels = false;
	};

	struct Controls {
		HWND prompt = nullptr;
		HWND negativePrompt = nullptr;
		HWND model = nullptr;
		HWND encoding = nullptr;
		HWND device = nullptr;
		HWND duration = nullptr;
		HWND download = nullptr;
		HWND settingsButton = nullptr;
		HWND action = nullptr;
		HWND progress = nullptr;
		HWND status = nullptr;
		GenerationSettings generation;
	};

	struct SettingsWindow {
		HWND owner = nullptr;
		Controls* controls = nullptr;
		HWND steps = nullptr;
		HWND seed = nullptr;
		HWND cfg = nullptr;
		HWND threads = nullptr;
		HWND padding = nullptr;
		HWND shift = nullptr;
		HWND keepModels = nullptr;
	};

	void CenterWindow(HWND hwnd) {
		RECT windowRect{};
		GetWindowRect(hwnd, &windowRect);
		HWND owner = GetWindow(hwnd, GW_OWNER);
		RECT centerRect{};
		if (owner && IsWindow(owner))
			GetWindowRect(owner, &centerRect);
		else {
			MONITORINFO info{ sizeof(info) };
			GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info);
			centerRect = info.rcWork;
		}
		const int width = windowRect.right - windowRect.left;
		const int height = windowRect.bottom - windowRect.top;
		SetWindowPos(
			hwnd, nullptr,
			centerRect.left + (centerRect.right - centerRect.left - width) / 2,
			centerRect.top + (centerRect.bottom - centerRect.top - height) / 2, 0, 0,
			SWP_NOZORDER | SWP_NOSIZE);
	}

	std::wstring GetText(HWND hwnd) {
		const int length = GetWindowTextLengthW(hwnd);
		std::wstring value(length, L'\0');
		if (length > 0)
			GetWindowTextW(hwnd, value.data(), length + 1);
		return value;
	}

	int GetInteger(HWND hwnd, int fallback) {
		try {
			return std::stoi(GetText(hwnd));
		}
		catch (...) {
			return fallback;
		}
	}

	long long GetInt64(HWND hwnd, long long fallback) {
		try {
			return std::stoll(GetText(hwnd));
		}
		catch (...) {
			return fallback;
		}
	}

	float GetReal(HWND hwnd, float fallback) {
		try {
			return std::stof(GetText(hwnd));
		}
		catch (...) {
			return fallback;
		}
	}

	HWND CreateEdit(HWND parent, int x, int y, int width, int height,
		const std::wstring& value, bool multiline = false) {
		AVS::TextEditSettings settings = AVS::TextEditSettings::Create();
		settings.IsMultiline = multiline;
		settings.IsVscroll = multiline;
		HWND edit = AVS::CreateTextEditMultiline(parent, GetModuleHandleW(nullptr), x,
			y, width, height, settings);
		SetWindowTextW(edit, value.c_str());
		return edit;
	}

	HWND CreateLabel(HWND parent, const wchar_t* text, int x, int y, int width,
		int height, AVS::LabelType type = AVS::LabelType::Disabled,
		DWORD alignment = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
		return AVS::CreateLabel(parent, GetModuleHandleW(nullptr), text, x, y, width,
			height, AVS::LabelSettings::Create(type, alignment));
	}

	AVS::ButtonSettings CancelButtonSettings() {
		AVS::ButtonSettings settings =
			AVS::ButtonSettings::Create(AVS::Buttons::Default);
		settings.Background = AVS::Color::MakeRGBA(255, 224, 224);
		settings.Border = AVS::Color::MakeRGBA(222, 164, 164);
		settings.BackgroundHover = AVS::Color::MakeRGBA(255, 210, 210);
		settings.BorderHover = AVS::Color::MakeRGBA(205, 125, 125);
		settings.BackgroundPressed = AVS::Color::MakeRGBA(244, 190, 190);
		settings.BorderPressed = AVS::Color::MakeRGBA(184, 100, 100);
		settings.TextColor = AVS::Color::MakeRGBA(128, 38, 38);
		settings.TextColorPressed = AVS::Color::MakeRGBA(96, 24, 24);
		settings.FocusColor = AVS::Color::MakeRGBA(190, 100, 100);
		return settings;
	}

	void SetBusy(Controls* controls, bool busy, bool lockInputs = true) {
		if (busy) {
			AVS::ProgressBar_SetPos(controls->progress, 0);
			ShowWindow(controls->progress, SW_SHOW);
		}
		else {
			ShowWindow(controls->progress, SW_HIDE);
			AVS::ProgressBar_SetPos(controls->progress, 0);
		}
		EnableWindow(controls->download, !busy);
		EnableWindow(controls->settingsButton, !busy);
		const bool enableInputs = !busy || !lockInputs;
		EnableWindow(controls->model, enableInputs);
		EnableWindow(controls->encoding, enableInputs);
		EnableWindow(controls->device, enableInputs);
		EnableWindow(controls->duration, enableInputs);
		AVS::Button_SetSettings(
			controls->action,
			busy ? CancelButtonSettings()
			: AVS::ButtonSettings::Create(AVS::Buttons::Primary),
			busy ? Translate(L"Cancel").c_str() : Translate(L"Generate").c_str());
	}

	void CloseSettings(HWND hwnd) {
		auto* state = reinterpret_cast<SettingsWindow*>(
			GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		if (state && state->owner && IsWindow(state->owner)) {
			EnableWindow(state->owner, TRUE);
			SetForegroundWindow(state->owner);
		}
		DestroyWindow(hwnd);
	}

	LRESULT CALLBACK SettingsWindowProc(HWND hwnd, UINT message, WPARAM wParam,
		LPARAM lParam) {
		auto* state = reinterpret_cast<SettingsWindow*>(
			GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		if (message == WM_NCCREATE) {
			auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
			state = reinterpret_cast<SettingsWindow*>(create->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
			return TRUE;
		}

		switch (message) {
		case WM_CREATE: {
			HINSTANCE instance = GetModuleHandleW(nullptr);
			const auto& values = state->controls->generation;
			struct Field {
				std::wstring label;
				int x;
				int width;
				HWND SettingsWindow::* member;
				std::wstring value;
			};
			const Field fields[] = { {Translate(L"Steps"), 15, 110, &SettingsWindow::steps,
									 std::to_wstring(values.steps)},
									{Translate(L"Seed"), 140, 120, &SettingsWindow::seed,
									 std::to_wstring(values.seed)},
									{Translate(L"CFG scale"), 275, 110, &SettingsWindow::cfg,
									 std::to_wstring(values.cfg)},
									{Translate(L"CPU threads"), 400, 110, &SettingsWindow::threads,
									 std::to_wstring(values.threads)},
									{Translate(L"Padding, sec"), 525, 110,
									 &SettingsWindow::padding,
									 std::to_wstring(values.padding)} };
			for (const auto& field : fields) {
				CreateLabel(hwnd, field.label.c_str(), field.x, 15, field.width, 20);
				state->*(field.member) =
					CreateEdit(hwnd, field.x, 38, field.width, 28, field.value);
			}

			CreateLabel(hwnd, Translate(L"Distribution shift").c_str(), 15, 82, 180, 20);
			std::vector<std::wstring> shifts{ L"LogSNR", L"Flux", L"Full", L"None" };
			int selected = 0;
			for (size_t i = 0; i < shifts.size(); ++i)
				if (shifts[i] == values.shift)
					selected = static_cast<int>(i);
			AVS::ComboBoxSettings comboSettings = AVS::ComboBoxSettings::Create();
			comboSettings.StartSelectedIndex = selected;
			state->shift = AVS::CreateComboBox(
				hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(204)), instance, 15,
				104, 180, 28, comboSettings, shifts);

			state->keepModels = CreateWindowExW(
				0, L"BUTTON", Translate(L"Keep models loaded in memory").c_str(),
				WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 220, 105, 255, 26,
				hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SETTINGS_KEEP)),
				instance, nullptr);
			SendMessageW(state->keepModels, WM_SETFONT,
				reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
				TRUE);
			SendMessageW(state->keepModels, BM_SETCHECK,
				values.keepModels ? BST_CHECKED : BST_UNCHECKED, 0);

			AVS::CreateButton(
				hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SETTINGS_CANCEL)),
				instance, Translate(L"Cancel").c_str(), 425, 157, 100, 30,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));
			AVS::CreateButton(
				hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SETTINGS_SAVE)),
				instance, Translate(L"Save").c_str(), 535, 157, 100, 30,
				AVS::ButtonSettings::Create(AVS::Buttons::Primary));
			return 0;
		}
		case WM_COMMAND:
			if (LOWORD(wParam) == ID_SETTINGS_SAVE && state && state->controls) {
				auto& values = state->controls->generation;
				values.steps = GetInteger(state->steps, values.steps);
				values.seed = GetInt64(state->seed, values.seed);
				values.cfg = GetReal(state->cfg, values.cfg);
				values.threads = GetInteger(state->threads, values.threads);
				values.padding = GetReal(state->padding, values.padding);
				values.shift = AVS::ComboBox_GetCurrentText(state->shift);
				values.keepModels =
					SendMessageW(state->keepModels, BM_GETCHECK, 0, 0) == BST_CHECKED;
				CloseSettings(hwnd);
				return 0;
			}
			if (LOWORD(wParam) == ID_SETTINGS_CANCEL) {
				CloseSettings(hwnd);
				return 0;
			}
			break;
		case WM_CLOSE:
			CloseSettings(hwnd);
			return 0;
		case WM_NCDESTROY:
			delete state;
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
			return DefWindowProcW(hwnd, message, wParam, lParam);
		}
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}

	void ShowSettingsWindow(HWND owner, Controls* controls) {
		static const wchar_t* className = L"StableAudio3SettingsWindowClass";
		WNDCLASSEXW existing{ sizeof(existing) };
		if (!GetClassInfoExW(GetModuleHandleW(nullptr), className, &existing)) {
			WNDCLASSEXW windowClass{ sizeof(windowClass) };
			windowClass.lpfnWndProc = SettingsWindowProc;
			windowClass.hInstance = GetModuleHandleW(nullptr);
			windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			windowClass.hIcon =
				reinterpret_cast<HICON>(SendMessageW(owner, WM_GETICON, ICON_BIG, 0));
			windowClass.hIconSm =
				reinterpret_cast<HICON>(SendMessageW(owner, WM_GETICON, ICON_SMALL, 0));
			AVS::Color background = AVS::Color::GetDefaultWindowBackground();
			windowClass.hbrBackground =
				CreateSolidBrush(RGB(background.R, background.G, background.B));
			windowClass.lpszClassName = className;
			RegisterClassExW(&windowClass);
		}

		auto* state = new SettingsWindow{ owner, controls };
		const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
		RECT rect{ 0, 0, 650, 205 };
		AdjustWindowRectEx(&rect, style, FALSE, 0);
		HWND window = CreateWindowExW(
			0, className, Translate(L"Stable Audio 3 Settings").c_str(), style, CW_USEDEFAULT,
			CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, owner,
			nullptr, GetModuleHandleW(nullptr), state);
		if (!window) {
			delete state;
			return;
		}
		EnableWindow(owner, FALSE);
		CenterWindow(window);
		ShowWindow(window, SW_SHOW);
		UpdateWindow(window);
	}

	LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam,
		LPARAM lParam) {
		auto* plugin = reinterpret_cast<CStableAudio3Plugin*>(
			GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		auto* controls =
			reinterpret_cast<Controls*>(GetPropW(hwnd, L"StableAudio3.Controls"));

		if (message == WM_NCCREATE) {
			auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA,
				reinterpret_cast<LONG_PTR>(create->lpCreateParams));
			return TRUE;
		}

		switch (message) {
		case WM_CREATE: {
			plugin = reinterpret_cast<CStableAudio3Plugin*>(
				GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			controls = new Controls();
			SetPropW(hwnd, L"StableAudio3.Controls", controls);
			HINSTANCE instance = GetModuleHandleW(nullptr);

			CreateLabel(hwnd, Translate(L"Prompt").c_str(), 15, 12, 180, 22);
			controls->prompt = CreateEdit(hwnd, 15, 36, 730, 150, L"", true);
			CreateLabel(hwnd, Translate(L"Negative prompt").c_str(), 15, 198, 145, 28);
			controls->negativePrompt = CreateEdit(hwnd, 165, 198, 580, 28, L"");

			CreateLabel(hwnd, Translate(L"Model").c_str(), 15, 245, 190, 20);
			CreateLabel(hwnd, Translate(L"Duration, sec").c_str(), 220, 245, 100, 20);
#if defined(_WIN32) && !defined(_WIN64)
			controls->model = AVS::CreateComboBox(
				hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_MODEL)), instance,
				15, 267, 190, 28, AVS::ComboBoxSettings::Create(),
				{ L"small-music", L"small-sfx" });
#else
			controls->model = AVS::CreateComboBox(
				hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_MODEL)), instance,
				15, 267, 190, 28, AVS::ComboBoxSettings::Create(),
				{ L"medium", L"small-music", L"small-sfx" });
#endif
			controls->duration = CreateEdit(hwnd, 220, 267, 100, 28, L"12.0");
			controls->download = AVS::CreateButton(
				hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DOWNLOAD)),
				instance, Translate(L"Download model").c_str(), 565, 267, 180, 28,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));

			CreateLabel(hwnd, Translate(L"Encoding").c_str(), 15, 311, 105, 20);
			CreateLabel(hwnd, Translate(L"Device").c_str(), 135, 311, 95, 20);
			controls->encoding = AVS::CreateComboBox(
				hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ENCODING)),
				instance, 15, 333, 105, 28, AVS::ComboBoxSettings::Create(),
				{ L"f32", L"f16" });
#if defined(_WIN32) && !defined(_WIN64)
			controls->device = AVS::CreateComboBox(
				hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DEVICE)),
				instance, 135, 333, 95, 28, AVS::ComboBoxSettings::Create(), { L"cpu" });
#else
			controls->device = AVS::CreateComboBox(
				hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DEVICE)),
				instance, 135, 333, 95, 28, AVS::ComboBoxSettings::Create(),
				{ L"cpu", L"gpu" });
#endif

			CreateLabel(hwnd, Translate(L"System resources and available acceleration").c_str(), 15, 379,
				730, 22);
			CreateLabel(hwnd, GetSystemSummary(plugin->moduleDirectory).c_str(), 15,
				403, 730, 122, AVS::LabelType::Enabled,
				DT_LEFT | DT_TOP | DT_WORDBREAK);
			controls->progress = AVS::CreateProgressBar(
				hwnd, instance, 15, 529, 730, 22, AVS::ProgressBarSettings::Create());
			AVS::ProgressBar_SetRange(controls->progress, 0, 100);
			controls->status =
				CreateLabel(hwnd, Translate(L"Ready").c_str(), 15, 562, 480, 28,
					AVS::LabelType::Enabled, DT_LEFT | DT_TOP | DT_WORDBREAK);
			controls->settingsButton = AVS::CreateButton(
				hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SETTINGS)),
				instance, Translate(L"Settings").c_str(), 515, 560, 105, 30,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));
			controls->action = AVS::CreateButton(
				hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ACTION)),
				instance, Translate(L"Generate").c_str(), 635, 560, 110, 30,
				AVS::ButtonSettings::Create(AVS::Buttons::Primary));
			SetBusy(controls, false);
			SetFocus(controls->prompt);
			plugin->window = hwnd;
			return 0;
		}

		case WM_COMMAND:
			if (!plugin || !controls)
				break;
			switch (LOWORD(wParam)) {
			case ID_SETTINGS:
				if (!plugin->busy)
					ShowSettingsWindow(hwnd, controls);
				return 0;
			case ID_DOWNLOAD:
				if (plugin->busy)
					return 0;
				AVS::ProgressBar_SetPos(controls->progress, 0);
				SetBusy(controls, true, false);
				AVS::Label_SetText(controls->status,
					Translate(L"Preparing model download...").c_str());
				plugin->StartDownload(AVS::ComboBox_GetCurrentText(controls->model),
					AVS::ComboBox_GetCurrentText(controls->encoding));
				return 0;
			case ID_ACTION:
				if (plugin->busy) {
					plugin->Cancel();
					EnableWindow(controls->action, FALSE);
				AVS::Label_SetText(controls->status,
						Translate(L"Cancelling...").c_str());
					return 0;
				}
				else {
					GenerationOptions options;
					options.prompt = GetText(controls->prompt);
					if (options.prompt.empty()) {
				AVS::Label_SetText(
                        controls->status,
							Translate(L"Enter a prompt before generation.").c_str());
						return 0;
					}
					options.negativePrompt = GetText(controls->negativePrompt);
					options.model = AVS::ComboBox_GetCurrentText(controls->model);
					options.encoding = AVS::ComboBox_GetCurrentText(controls->encoding);
					options.device = AVS::ComboBox_GetCurrentText(controls->device);
					const float durationSeconds = GetReal(controls->duration, 0.0f);
					if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0f) {
				AVS::Label_SetText(
                        controls->status,
							Translate(L"Duration must be greater than zero.").c_str());
						return 0;
					}
					const float maximumDuration =
						options.model == L"medium" ? 380.0f : 120.0f;
					if (durationSeconds > maximumDuration) {
						AVS::Label_SetText(
							controls->status,
							L"Maximum duration for " + options.model + L" is " +
							std::to_wstring(static_cast<int>(maximumDuration)) +
							L" seconds.");
						return 0;
					}
					constexpr double sampleRate = 44100.0;
					constexpr double samplesPerLatentFrame = 4096.0;
					options.frames = (std::max)(
						1, static_cast<int>(std::ceil(static_cast<double>(durationSeconds) *
							sampleRate / samplesPerLatentFrame)));
					options.steps = controls->generation.steps;
					options.seed = controls->generation.seed;
					options.cfg = controls->generation.cfg;
					options.threads = controls->generation.threads;
					options.padding = controls->generation.padding;
					options.distShift = controls->generation.shift;
					options.keepModels = controls->generation.keepModels;
					if (!IsModelSetPresent(plugin->workDirectory / L"models", options.model,
						options.encoding)) {
						AVS::Label_SetText(
							controls->status,
							Translate(L"Model files are missing. Click Download model first.")
								.c_str());
						return 0;
					}
					AVS::ProgressBar_SetPos(controls->progress, 0);
					SetBusy(controls, true);
				AVS::Label_SetText(controls->status,
						Translate(L"Starting generation...").c_str());
					plugin->StartGeneration(std::move(options));
					return 0;
				}
			default:
				break;
			}
			break;

		case WM_SA3_STATUS:
			if (plugin && controls) {
				std::unique_ptr<UiStatus> status(reinterpret_cast<UiStatus*>(lParam));
				AVS::Label_SetText(controls->status, status->text);
				if (status->progress >= 0)
					AVS::ProgressBar_SetPos(controls->progress, status->progress);
				if (status->finished) {
					EnableWindow(controls->action, TRUE);
					SetBusy(controls, false);
					if (status->success && plugin->callback) {
						plugin->callback(export_str(L"StableAudio3.plugin"),
							export_str(plugin->lastOutput.c_str()), 0,
							plugin->callbackContext);
						if (plugin->window == hwnd && IsWindow(hwnd))
							PostMessageW(hwnd, WM_SA3_CLOSE_AFTER_CALLBACK, 0, 0);
					}
				}
			}
			return 0;

		case WM_SA3_CLOSE_AFTER_CALLBACK:
			if (plugin && plugin->window == hwnd)
				DestroyWindow(hwnd);
			return 0;

		case WM_CLOSE:
			if (plugin && plugin->busy) {
				plugin->Cancel();
				if (controls) {
					EnableWindow(controls->action, FALSE);
				AVS::Label_SetText(controls->status,
						Translate(L"Cancelling...").c_str());
				}
				return 0;
			}
			DestroyWindow(hwnd);
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		case WM_NCDESTROY:
			if (plugin) {
				plugin->window = nullptr;
				plugin->JoinWorker();
			}
			delete controls;
			RemovePropW(hwnd, L"StableAudio3.Controls");
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
			return DefWindowProcW(hwnd, message, wParam, lParam);
		}
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
} // namespace

void ShowStableAudioWindow(CStableAudio3Plugin* plugin) {
	ACTCTXW activation{ sizeof(activation) };
	activation.dwFlags =
		ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
	activation.lpResourceName = MAKEINTRESOURCEW(1);
	activation.hModule = g_module;

	HANDLE activationContext = CreateActCtxW(&activation);
	ULONG_PTR activationCookie = 0;
	const bool activationActive =
		activationContext != INVALID_HANDLE_VALUE &&
		ActivateActCtx(activationContext, &activationCookie);

	INITCOMMONCONTROLSEX commonControls{
		sizeof(commonControls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS };
	InitCommonControlsEx(&commonControls);

	static const wchar_t* className = L"StableAudio3MainWindowClass";
	WNDCLASSEXW existing{ sizeof(existing) };
	if (!GetClassInfoExW(GetModuleHandleW(nullptr), className, &existing)) {
		WNDCLASSEXW windowClass{ sizeof(windowClass) };
		windowClass.lpfnWndProc = WindowProc;
		windowClass.hInstance = GetModuleHandleW(nullptr);
		windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		const auto iconPath = plugin->workDirectory / L"icon_internal.ico";
		windowClass.hIcon =
			static_cast<HICON>(LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON, 32,
				32, LR_LOADFROMFILE | LR_DEFAULTCOLOR));
		windowClass.hIconSm =
			static_cast<HICON>(LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON, 16,
				16, LR_LOADFROMFILE | LR_DEFAULTCOLOR));
		AVS::Color background = AVS::Color::GetDefaultWindowBackground();
		windowClass.hbrBackground =
			CreateSolidBrush(RGB(background.R, background.G, background.B));
		windowClass.lpszClassName = className;
		RegisterClassExW(&windowClass);
	}

	const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	RECT rect{ 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
	AdjustWindowRectEx(&rect, style, FALSE, 0);
	HWND hwnd = CreateWindowExW(
		0, className, L"StableAudio3", style, CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
		GetModuleHandleW(nullptr), plugin);
	if (hwnd) {
		SetWindowTextW(hwnd, L"StableAudio3");
		CenterWindow(hwnd);
		ShowWindow(hwnd, SW_SHOW);
		UpdateWindow(hwnd);

		MSG message{};
		while (GetMessageW(&message, nullptr, 0, 0) > 0) {
			HWND settings = FindWindowW(L"StableAudio3SettingsWindowClass", nullptr);
			if (settings && IsWindow(settings) &&
				IsDialogMessageW(settings, &message))
				continue;

			if (!IsDialogMessageW(hwnd, &message)) {
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
		}
	}

	if (activationActive)
		DeactivateActCtx(0, activationCookie);
	if (activationContext != INVALID_HANDLE_VALUE)
		ReleaseActCtx(activationContext);
}
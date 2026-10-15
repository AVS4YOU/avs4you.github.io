#include "ui.h"
#include "../../../sdk/ui/winapi/ui.h"
#include "export_utils.h"

#include <commctrl.h>
#include <commdlg.h>
#include <memory>
#include <shellapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace {
	constexpr wchar_t WINDOW_CLASS[] = L"AiSubtitlesMainWindowClass";
	constexpr wchar_t CONTROLS_PROPERTY[] = L"AiSubtitles.Controls";
	constexpr wchar_t TITLE[] = L"AI Subtitles";
	constexpr int CLIENT_WIDTH = 640;
	constexpr int CLIENT_HEIGHT = 442;

	constexpr int ID_BROWSE = 101;
	constexpr int ID_LANGUAGE = 102;
	constexpr int ID_QUALITY = 103;
	constexpr int ID_TRANSLATE = 104;
	constexpr int ID_OPEN_FOLDER = 105;
	constexpr int ID_ACTION = 106;
	constexpr int ID_CONTENT = 107;
	constexpr int ID_GPU = 108;

	struct Controls {
		HWND file = nullptr;
		HWND browse = nullptr;
		HWND language = nullptr;
		HWND quality = nullptr;
		HWND content = nullptr;
		HWND translate = nullptr;
		HWND gpu = nullptr;
		bool gpuAvailable = false;   // the x86 package has no Vulkan build
		HWND output = nullptr;
		HWND progress = nullptr;
		HWND status = nullptr;
		HWND openFolder = nullptr;
		HWND action = nullptr;
		std::filesystem::path input;
		std::filesystem::path result;
		bool closeWhenDone = false;  // closed while working: close once it stops
	};

	HWND CreateLabel(HWND parent, const std::wstring& text, int x, int y, int width, int height,
		AVS::LabelType type = AVS::LabelType::Disabled,
		DWORD alignment = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
		return AVS::CreateLabel(parent, GetModuleHandleW(nullptr), text.c_str(), x, y, width, height,
			AVS::LabelSettings::Create(type, alignment));
	}

	HWND CreateCheckBox(HWND parent, int id, const std::wstring& text, int x, int y, int width,
		bool checked) {
		HWND box = CreateWindowExW(0, L"BUTTON", text.c_str(),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, y, width, 24, parent,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
		SendMessageW(box, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
		SendMessageW(box, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
		return box;
	}

	AVS::ButtonSettings CancelButtonSettings() {
		AVS::ButtonSettings settings = AVS::ButtonSettings::Create(AVS::Buttons::Default);
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

	void CenterWindow(HWND hwnd) {
		RECT window{};
		GetWindowRect(hwnd, &window);
		RECT area{};
		HWND owner = GetWindow(hwnd, GW_OWNER);
		if (owner && IsWindow(owner))
			GetWindowRect(owner, &area);
		else {
			MONITORINFO info{ sizeof(info) };
			GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info);
			area = info.rcWork;
		}
		const int width = window.right - window.left, height = window.bottom - window.top;
		SetWindowPos(hwnd, nullptr, area.left + (area.right - area.left - width) / 2,
			area.top + (area.bottom - area.top - height) / 2, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	}

	std::wstring QualityLabel(const WhisperModel& model) {
		return Translate(model.label) + L" - " + model.id + L" (" +
			std::to_wstring(model.size / (1000 * 1000)) + L" MB)";
	}

	void SetBusy(Controls* controls, bool busy) {
		ShowWindow(controls->progress, busy ? SW_SHOW : SW_HIDE);
		if (busy)
			AVS::ProgressBar_SetPos(controls->progress, 0);
		for (HWND input : { controls->browse, controls->language, controls->quality,
			controls->content, controls->translate })
			EnableWindow(input, !busy);
		EnableWindow(controls->gpu, !busy && controls->gpuAvailable);
		if (busy)
			EnableWindow(controls->openFolder, FALSE);
		AVS::Button_SetSettings(controls->action,
			busy ? CancelButtonSettings() : AVS::ButtonSettings::Create(AVS::Buttons::Primary),
			Translate(busy ? L"Cancel" : L"Create subtitles"));
	}

	void ShowInput(Controls* controls) {
		AVS::Label_SetText(controls->file,
			controls->input.empty() ? Translate(L"No file selected") : controls->input.wstring());
		AVS::Label_SetText(controls->output, controls->input.empty() ? std::wstring()
			: Translate(L"Subtitles will be saved as") + L" " +
			DefaultSubtitlePath(controls->input).filename().wstring());
	}

	bool ChooseFile(HWND owner, Controls* controls) {
		std::wstring filter = Translate(L"Video and audio files");
		filter += L'\0';
		filter += L"*.mp4;*.mov;*.mkv;*.avi;*.wmv;*.webm;*.m4v;*.mpg;*.mpeg;*.ts;*.flv;*.3gp;"
			L"*.mp3;*.wav;*.m4a;*.aac;*.flac;*.ogg;*.opus;*.wma";
		filter += L'\0';
		filter += Translate(L"All files");
		filter += L'\0';
		filter += L"*.*";
		filter += L'\0';
		wchar_t path[32768]{};
		OPENFILENAMEW dialog{ sizeof(dialog) };
		dialog.hwndOwner = owner;
		dialog.lpstrFilter = filter.c_str();
		dialog.lpstrFile = path;
		dialog.nMaxFile = static_cast<DWORD>(std::size(path));
		dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY;
		if (!GetOpenFileNameW(&dialog))
			return false;
		controls->input = path;
		return true;
	}

	void Start(HWND hwnd, CSubtitlesPlugin* plugin, Controls* controls) {
		if (controls->input.empty()) {
			AVS::Label_SetText(controls->status, Translate(L"Choose a video or audio file first."));
			return;
		}
		SubtitleJob job;
		job.input = controls->input;
		job.output = DefaultSubtitlePath(job.input);
		std::error_code error;
		if (std::filesystem::exists(job.output, error)) {
			const std::wstring question = job.output.filename().wstring() + L"\r\n\r\n" +
				Translate(L"This file already exists. Replace it?");
			if (MessageBoxW(hwnd, question.c_str(), Translate(TITLE).c_str(),
				MB_YESNO | MB_ICONQUESTION) != IDYES)
				return;
		}

		const auto& models = WhisperModels();
		const auto& languages = SubtitleLanguages();
		const int quality = AVS::ComboBox_GetCurrent(controls->quality);
		const int language = AVS::ComboBox_GetCurrent(controls->language);
		plugin->model = models[(std::max)(0, (std::min)(quality, (int)models.size() - 1))].id;
		plugin->language = languages[(std::max)(0, (std::min)(language, (int)languages.size() - 1))].code;
		plugin->translate = SendMessageW(controls->translate, BM_GETCHECK, 0, 0) == BST_CHECKED;
		plugin->speechOnly = AVS::ComboBox_GetCurrent(controls->content) != 1;
		if (controls->gpuAvailable)
			plugin->useGpu = SendMessageW(controls->gpu, BM_GETCHECK, 0, 0) == BST_CHECKED;
		plugin->SaveSettings();
		job.model = plugin->model;
		job.language = plugin->language;
		job.translate = plugin->translate;
		job.speechOnly = plugin->speechOnly;
		job.useGpu = plugin->useGpu;

		controls->result.clear();
		SetBusy(controls, true);
		AVS::Label_SetText(controls->status, Translate(L"Starting..."));
		plugin->Start(std::move(job));
	}

	LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
		auto* plugin = reinterpret_cast<CSubtitlesPlugin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		auto* controls = reinterpret_cast<Controls*>(GetPropW(hwnd, CONTROLS_PROPERTY));

		switch (message) {
		case WM_NCCREATE: {
			auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
			break;  // DefWindowProc sets the title
		}
		case WM_CTLCOLORSTATIC: {
			// the checkbox on the window's own background, not the system grey
			static HBRUSH background = [] {
				const AVS::Color color = AVS::Color::GetDefaultWindowBackground();
				return CreateSolidBrush(RGB(color.R, color.G, color.B));
			}();
			SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
			return reinterpret_cast<LRESULT>(background);
		}
		case WM_CREATE: {
			plugin = reinterpret_cast<CSubtitlesPlugin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			controls = new Controls();
			SetPropW(hwnd, CONTROLS_PROPERTY, controls);
			HINSTANCE instance = GetModuleHandleW(nullptr);

			CreateLabel(hwnd, Translate(L"Video or audio file"), 15, 12, 400, 22);
			controls->file = CreateLabel(hwnd, L"", 15, 36, 480, 30, AVS::LabelType::Enabled,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
			controls->browse = AVS::CreateButton(hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BROWSE)), instance,
				Translate(L"Choose file...").c_str(), 505, 36, 120, 30,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));

			CreateLabel(hwnd, Translate(L"Spoken language"), 15, 84, 220, 22);
			CreateLabel(hwnd, Translate(L"Quality"), 250, 84, 300, 22);
			std::vector<std::wstring> languageItems;
			int languageIndex = 0;
			const auto& languages = SubtitleLanguages();
			for (size_t index = 0; index < languages.size(); ++index) {
				languageItems.push_back(index == 0 ? Translate(L"Detect automatically")
					: std::wstring(languages[index].name));
				if (plugin->language == languages[index].code)
					languageIndex = static_cast<int>(index);
			}
			AVS::ComboBoxSettings languageSettings = AVS::ComboBoxSettings::Create();
			languageSettings.StartSelectedIndex = languageIndex;
			controls->language = AVS::CreateComboBox(hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LANGUAGE)), instance, 15, 108, 220,
				28, languageSettings, languageItems);

			std::vector<std::wstring> qualityItems;
			int qualityIndex = 0;
			const auto& models = WhisperModels();
			for (size_t index = 0; index < models.size(); ++index) {
				qualityItems.push_back(QualityLabel(models[index]));
				if (plugin->model == models[index].id)
					qualityIndex = static_cast<int>(index);
			}
			AVS::ComboBoxSettings qualitySettings = AVS::ComboBoxSettings::Create();
			qualitySettings.StartSelectedIndex = qualityIndex;
			controls->quality = AVS::CreateComboBox(hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_QUALITY)), instance, 250, 108, 375,
				28, qualitySettings, qualityItems);

			// speech: VAD keeps cues to what is said; it does not hear singing
			CreateLabel(hwnd, Translate(L"What is in the file"), 15, 148, 610, 22);
			AVS::ComboBoxSettings contentSettings = AVS::ComboBoxSettings::Create();
			contentSettings.StartSelectedIndex = plugin->speechOnly ? 0 : 1;
			controls->content = AVS::CreateComboBox(hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CONTENT)), instance, 15, 172, 610, 28,
				contentSettings, { Translate(L"Speech: films, videos, interviews"),
					Translate(L"Music and songs") });

			controls->translate = CreateCheckBox(hwnd, ID_TRANSLATE,
				Translate(L"Translate subtitles into English"), 15, 216, 300, plugin->translate);
			controls->gpuAvailable = HasGpuEngine(plugin->Tools());
			controls->gpu = CreateCheckBox(hwnd, ID_GPU, Translate(L"Use the graphics card (GPU)"),
				330, 216, 295, controls->gpuAvailable && plugin->useGpu);
			CreateLabel(hwnd,
				Translate(L"The speech model is downloaded once, the first time it is used. "
					L"Everything runs on this computer - nothing is uploaded."),
				15, 246, 610, 40, AVS::LabelType::Disabled, DT_LEFT | DT_TOP | DT_WORDBREAK);
			controls->output = CreateLabel(hwnd, L"", 15, 290, 610, 22);

			controls->progress = AVS::CreateProgressBar(hwnd, instance, 15, 320, 610, 20,
				AVS::ProgressBarSettings::Create());
			AVS::ProgressBar_SetRange(controls->progress, 0, 100);
			controls->status = CreateLabel(hwnd, Translate(L"Ready"), 15, 346, 610, 40,
				AVS::LabelType::Enabled, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

			controls->openFolder = AVS::CreateButton(hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OPEN_FOLDER)), instance,
				Translate(L"Show file").c_str(), 15, 398, 150, 30,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));
			controls->action = AVS::CreateButton(hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ACTION)), instance,
				Translate(L"Create subtitles").c_str(), 445, 398, 180, 30,
				AVS::ButtonSettings::Create(AVS::Buttons::Primary));
			EnableWindow(controls->openFolder, FALSE);
			SetBusy(controls, false);
			ShowInput(controls);
			plugin->window = hwnd;
			return 0;
		}

		case WM_COMMAND:
			if (!plugin || !controls)
				break;
			switch (LOWORD(wParam)) {
			case ID_BROWSE:
				if (!plugin->busy && ChooseFile(hwnd, controls)) {
					ShowInput(controls);
					AVS::Label_SetText(controls->status, Translate(L"Ready"));
				}
				return 0;
			case ID_OPEN_FOLDER:
				if (!controls->result.empty()) {
					// Explorer with the new file selected
					const std::wstring arguments = L"/select,\"" + controls->result.wstring() + L"\"";
					ShellExecuteW(hwnd, L"open", L"explorer.exe", arguments.c_str(), nullptr,
						SW_SHOWNORMAL);
				}
				return 0;
			case ID_ACTION:
				if (plugin->busy) {
					plugin->Cancel();
					EnableWindow(controls->action, FALSE);
					AVS::Label_SetText(controls->status, Translate(L"Cancelling..."));
				}
				else
					Start(hwnd, plugin, controls);
				return 0;
			}
			break;

		case WM_SUBTITLES_STATUS:
			if (controls) {
				std::unique_ptr<UiStatus> status(reinterpret_cast<UiStatus*>(lParam));
				AVS::Label_SetText(controls->status, status->text);
				if (status->progress >= 0)
					AVS::ProgressBar_SetPos(controls->progress, status->progress);
				if (status->finished) {
					EnableWindow(controls->action, TRUE);
					SetBusy(controls, false);
					if (status->success) {
						controls->result = DefaultSubtitlePath(controls->input);
						EnableWindow(controls->openFolder, TRUE);
					}
					if (controls->closeWhenDone)
						DestroyWindow(hwnd);
				}
			}
			return 0;

		case WM_CLOSE:
			if (plugin && plugin->busy) {
				plugin->Cancel();
				if (controls) {
					controls->closeWhenDone = true;
					EnableWindow(controls->action, FALSE);
					AVS::Label_SetText(controls->status, Translate(L"Cancelling..."));
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
			RemovePropW(hwnd, CONTROLS_PROPERTY);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
			break;
		}
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
} // namespace

void ShowSubtitlesWindow(CSubtitlesPlugin* plugin) {
	HWND host = GetActiveWindow();
	if (!host) {
		HWND foreground = GetForegroundWindow();
		DWORD processId = 0;
		if (foreground)
			GetWindowThreadProcessId(foreground, &processId);
		if (processId == GetCurrentProcessId())
			host = foreground;
	}
	if (host)
		host = GetAncestor(host, GA_ROOT);
	const bool hostWasEnabled = host && IsWindowEnabled(host);

	// common controls v6 from the module manifest, for the themed checkbox
	ACTCTXW activation{ sizeof(activation) };
	activation.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
	activation.lpResourceName = MAKEINTRESOURCEW(1);
	activation.hModule = g_module;
	HANDLE context = CreateActCtxW(&activation);
	ULONG_PTR cookie = 0;
	const bool activated = context != INVALID_HANDLE_VALUE && ActivateActCtx(context, &cookie);

	INITCOMMONCONTROLSEX common{ sizeof(common), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS };
	InitCommonControlsEx(&common);

	WNDCLASSEXW existing{ sizeof(existing) };
	if (!GetClassInfoExW(GetModuleHandleW(nullptr), WINDOW_CLASS, &existing)) {
		WNDCLASSEXW windowClass{ sizeof(windowClass) };
		windowClass.lpfnWndProc = WindowProc;
		windowClass.hInstance = GetModuleHandleW(nullptr);
		windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		const auto icon = plugin->workDirectory / L"icon_internal.ico";
		windowClass.hIcon = static_cast<HICON>(
			LoadImageW(nullptr, icon.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
		windowClass.hIconSm = static_cast<HICON>(
			LoadImageW(nullptr, icon.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE));
		const AVS::Color background = AVS::Color::GetDefaultWindowBackground();
		windowClass.hbrBackground = CreateSolidBrush(RGB(background.R, background.G, background.B));
		windowClass.lpszClassName = WINDOW_CLASS;
		RegisterClassExW(&windowClass);
	}

	const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	RECT rect{ 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
	AdjustWindowRectEx(&rect, style, FALSE, 0);
	HWND hwnd = CreateWindowExW(0, WINDOW_CLASS, Translate(TITLE).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top, host, nullptr, GetModuleHandleW(nullptr),
		plugin);
	if (hwnd) {
		if (hostWasEnabled)
			EnableWindow(host, FALSE);
		CenterWindow(hwnd);
		ShowWindow(hwnd, SW_SHOW);
		UpdateWindow(hwnd);
		MSG message{};
		while (GetMessageW(&message, nullptr, 0, 0) > 0) {
			if (!IsDialogMessageW(hwnd, &message)) {
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
		}
	}
	if (hostWasEnabled && host && IsWindow(host)) {
		EnableWindow(host, TRUE);
		SetForegroundWindow(host);
	}
	if (activated)
		DeactivateActCtx(0, cookie);
	if (context != INVALID_HANDLE_VALUE)
		ReleaseActCtx(context);
}

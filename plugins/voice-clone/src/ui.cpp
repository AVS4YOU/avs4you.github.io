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
	constexpr wchar_t WINDOW_CLASS[] = L"VoiceCloneMainWindowClass";
	constexpr wchar_t CONTROLS_PROPERTY[] = L"VoiceClone.Controls";
	constexpr wchar_t TITLE[] = L"Voice Clone";
	constexpr wchar_t PLUGIN_ID[] = L"VoiceClone.plugin";
	constexpr int CLIENT_WIDTH = 640;
	constexpr int CLIENT_HEIGHT = 524;
	constexpr int PITCH_RANGE = 12;

	constexpr int ID_BROWSE = 101;
	constexpr int ID_VOICE = 102;
	constexpr int ID_QUALITY = 103;
	constexpr int ID_PITCH = 104;
	constexpr int ID_FORMAT = 105;
	constexpr int ID_MATCH = 106;
	constexpr int ID_GPU = 107;
	constexpr int ID_ADD = 108;
	constexpr int ID_OPEN_FOLDER = 109;
	constexpr int ID_ACTION = 110;

	struct Controls {
		HWND file = nullptr;
		HWND browse = nullptr;
		HWND voiceFile = nullptr;
		HWND voiceBrowse = nullptr;
		HWND quality = nullptr;
		HWND pitch = nullptr;
		HWND format = nullptr;
		HWND match = nullptr;
		HWND gpu = nullptr;
		HWND add = nullptr;
		HWND note = nullptr;
		HWND output = nullptr;
		HWND progress = nullptr;
		HWND status = nullptr;
		HWND openFolder = nullptr;
		HWND action = nullptr;
		std::filesystem::path input;
		VoiceJob job;                // the running or last job
		bool closeWhenDone = false;  // closed while working: close once it stops
	};

	HWND CreateLabel(HWND parent, const std::wstring& text, int x, int y, int width, int height,
		AVS::LabelType type = AVS::LabelType::Disabled,
		DWORD alignment = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
		return AVS::CreateLabel(parent, GetModuleHandleW(nullptr), text.c_str(), x, y, width, height,
			AVS::LabelSettings::Create(type, alignment));
	}

	HWND CreateCheckBox(HWND parent, int id, const std::wstring& text, int x, int y, int width, bool checked) {
		HWND box = CreateWindowExW(0, L"BUTTON", text.c_str(),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, y, width, 24, parent,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
		SendMessageW(box, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
		SendMessageW(box, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
		return box;
	}

	bool IsChecked(HWND box) { return SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED; }

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

	const VoiceFormat& ChosenFormat(const CVoiceClonePlugin* plugin) {
		const VoiceFormat* format = FindVoiceFormat(plugin->format);
		return format ? *format : VoiceFormats().front();
	}

	void SetBusy(Controls* controls, bool busy) {
		ShowWindow(controls->progress, busy ? SW_SHOW : SW_HIDE);
		if (busy)
			AVS::ProgressBar_SetPos(controls->progress, 0);
		for (HWND input : { controls->browse, controls->voiceBrowse, controls->quality, controls->pitch,
			controls->format, controls->match, controls->gpu, controls->add })
			EnableWindow(input, !busy);
		if (busy)
			EnableWindow(controls->openFolder, FALSE);
		AVS::Button_SetSettings(controls->action,
			busy ? CancelButtonSettings() : AVS::ButtonSettings::Create(AVS::Buttons::Primary),
			Translate(busy ? L"Cancel" : L"Change voice"));
	}

	void ShowFiles(const CVoiceClonePlugin* plugin, Controls* controls) {
		AVS::Label_SetText(controls->file,
			controls->input.empty() ? Translate(L"No file selected") : controls->input.wstring());
		AVS::Label_SetText(controls->voiceFile,
			plugin->voice.empty() ? Translate(L"No file selected") : plugin->voice.wstring());
		AVS::Label_SetText(controls->output, controls->input.empty() ? std::wstring()
			: Translate(L"The result will be saved as") + L" \"" +
			DefaultVoiceOutput(controls->input, ChosenFormat(plugin)).filename().wstring() + L"\"");
	}

	void ShowNote(const CVoiceClonePlugin* plugin, Controls* controls) {
		const std::uint64_t missing = MissingVoiceModelBytes(plugin->Tools());
		std::wstring note = Translate(L"Everything runs on this computer - nothing is uploaded.");
		if (missing)
			note = Translate(L"The voice model is downloaded once, the first time it is used:") + L" " +
				std::to_wstring((missing + 512 * 1024) / (1024 * 1024)) + L" MB. " + note;
		AVS::Label_SetText(controls->note, note);
	}

	bool ChooseFile(HWND owner, std::filesystem::path& file) {
		std::wstring filter = Translate(L"Music and video files");
		filter += L'\0';
		filter += L"*.mp3;*.wav;*.flac;*.m4a;*.aac;*.ogg;*.opus;*.wma;*.aiff;*.aif;"
			L"*.mp4;*.mov;*.mkv;*.avi;*.wmv;*.webm;*.m4v;*.mpg;*.mpeg;*.flv";
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
		file = path;
		return true;
	}

	void ReadChoices(CVoiceClonePlugin* plugin, Controls* controls) {
		const auto& qualities = VoiceQualities();
		const auto& formats = VoiceFormats();
		const int quality = AVS::ComboBox_GetCurrent(controls->quality);
		const int format = AVS::ComboBox_GetCurrent(controls->format);
		plugin->quality = qualities[(std::max)(0, (std::min)(quality, (int)qualities.size() - 1))].id;
		plugin->format = formats[(std::max)(0, (std::min)(format, (int)formats.size() - 1))].id;
		plugin->pitch = (std::max)(0, AVS::ComboBox_GetCurrent(controls->pitch)) - PITCH_RANGE;
		plugin->matchVoicePitch = IsChecked(controls->match);
		plugin->useGpu = IsChecked(controls->gpu);
		plugin->addToProject = IsChecked(controls->add);
	}

	void Start(HWND hwnd, CVoiceClonePlugin* plugin, Controls* controls) {
		std::error_code error;
		if (controls->input.empty()) {
			AVS::Label_SetText(controls->status, Translate(L"Choose the vocals first."));
			return;
		}
		if (plugin->voice.empty() || !std::filesystem::exists(plugin->voice, error)) {
			AVS::Label_SetText(controls->status, Translate(L"Choose a voice sample first."));
			return;
		}
		ReadChoices(plugin, controls);
		plugin->SaveSettings();

		VoiceJob job;
		job.input = controls->input;
		job.voice = plugin->voice;
		job.output = DefaultVoiceOutput(job.input, ChosenFormat(plugin));
		job.quality = plugin->quality;
		job.pitch = plugin->pitch;
		job.matchVoicePitch = plugin->matchVoicePitch;
		job.useGpu = plugin->useGpu;
		if (std::filesystem::exists(job.output, error)) {
			const std::wstring question = Translate(L"This file already exists. Replace it?") + L"\r\n" +
				job.output.filename().wstring();
			if (MessageBoxW(hwnd, question.c_str(), Translate(TITLE).c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES)
				return;
		}

		controls->job = job;
		SetBusy(controls, true);
		AVS::Label_SetText(controls->status, Translate(L"Starting..."));
		plugin->Start(std::move(job));
	}

	// the result into the host's media library, as the other content plugins
	// hand over what they made
	void AddToProject(CVoiceClonePlugin* plugin, const Controls* controls) {
		if (!plugin->addToProject || !plugin->callback)
			return;
		plugin->callback(export_str(PLUGIN_ID), export_str(controls->job.output.c_str()), 0, plugin->callbackContext);
	}

	LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
		auto* plugin = reinterpret_cast<CVoiceClonePlugin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		auto* controls = reinterpret_cast<Controls*>(GetPropW(hwnd, CONTROLS_PROPERTY));

		switch (message) {
		case WM_NCCREATE: {
			auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
			break;  // DefWindowProc sets the title
		}
		case WM_CTLCOLORSTATIC: {
			// the checkboxes on the window's own background, not the system grey
			static HBRUSH background = [] {
				const AVS::Color color = AVS::Color::GetDefaultWindowBackground();
				return CreateSolidBrush(RGB(color.R, color.G, color.B));
			}();
			SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
			return reinterpret_cast<LRESULT>(background);
		}
		case WM_CREATE: {
			plugin = reinterpret_cast<CVoiceClonePlugin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			controls = new Controls();
			SetPropW(hwnd, CONTROLS_PROPERTY, controls);
			HINSTANCE instance = GetModuleHandleW(nullptr);
			const auto defaultButton = AVS::ButtonSettings::Create(AVS::Buttons::Default);

			CreateLabel(hwnd, Translate(L"Vocals to sing in the new voice"), 15, 12, 480, 22);
			controls->file = CreateLabel(hwnd, L"", 15, 36, 480, 30, AVS::LabelType::Enabled,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
			controls->browse = AVS::CreateButton(hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BROWSE)),
				instance, Translate(L"Choose file...").c_str(), 505, 36, 120, 30, defaultButton);
			CreateLabel(hwnd, Translate(L"A voice without music: take the vocals out of a song with Stem Splitter first."),
				15, 68, 610, 22);

			CreateLabel(hwnd, Translate(L"Voice sample"), 15, 98, 480, 22);
			controls->voiceFile = CreateLabel(hwnd, L"", 15, 122, 480, 30, AVS::LabelType::Enabled,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
			controls->voiceBrowse = AVS::CreateButton(hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_VOICE)),
				instance, Translate(L"Choose file...").c_str(), 505, 122, 120, 30, defaultButton);
			CreateLabel(hwnd, Translate(L"5-15 seconds of one person speaking or singing, clean, without music. "
				L"Use only a voice you have the right to use."), 15, 154, 610, 36, AVS::LabelType::Disabled,
				DT_LEFT | DT_TOP | DT_WORDBREAK);

			CreateLabel(hwnd, Translate(L"Quality"), 15, 196, 200, 22);
			CreateLabel(hwnd, Translate(L"Pitch, semitones"), 230, 196, 200, 22);
			CreateLabel(hwnd, Translate(L"Format"), 445, 196, 180, 22);
			std::vector<std::wstring> qualityItems;
			int qualityIndex = 0;
			const auto& qualities = VoiceQualities();
			for (size_t index = 0; index < qualities.size(); ++index) {
				qualityItems.push_back(Translate(qualities[index].label));
				if (plugin->quality == qualities[index].id)
					qualityIndex = static_cast<int>(index);
			}
			AVS::ComboBoxSettings qualitySettings = AVS::ComboBoxSettings::Create();
			qualitySettings.StartSelectedIndex = qualityIndex;
			controls->quality = AVS::CreateComboBox(hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_QUALITY)),
				instance, 15, 220, 200, 28, qualitySettings, qualityItems);

			std::vector<std::wstring> pitchItems;
			for (int pitch = -PITCH_RANGE; pitch <= PITCH_RANGE; ++pitch)
				pitchItems.push_back(pitch == 0 ? L"0 (" + Translate(L"original key") + L")"
					: (pitch > 0 ? L"+" : L"") + std::to_wstring(pitch));
			AVS::ComboBoxSettings pitchSettings = AVS::ComboBoxSettings::Create();
			pitchSettings.StartSelectedIndex = plugin->pitch + PITCH_RANGE;
			controls->pitch = AVS::CreateComboBox(hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PITCH)),
				instance, 230, 220, 200, 28, pitchSettings, pitchItems);

			std::vector<std::wstring> formatItems;
			int formatIndex = 0;
			const auto& formats = VoiceFormats();
			for (size_t index = 0; index < formats.size(); ++index) {
				formatItems.push_back(formats[index].label);
				if (plugin->format == formats[index].id)
					formatIndex = static_cast<int>(index);
			}
			AVS::ComboBoxSettings formatSettings = AVS::ComboBoxSettings::Create();
			formatSettings.StartSelectedIndex = formatIndex;
			controls->format = AVS::CreateComboBox(hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FORMAT)),
				instance, 445, 220, 180, 28, formatSettings, formatItems);

			controls->match = CreateCheckBox(hwnd, ID_MATCH,
				Translate(L"Move the melody into the voice's own range"), 15, 262, 610, plugin->matchVoicePitch);
			controls->gpu = CreateCheckBox(hwnd, ID_GPU, Translate(L"Use the graphics card (GPU)"),
				15, 288, 610, plugin->useGpu);
			controls->add = CreateCheckBox(hwnd, ID_ADD, Translate(L"Add the result to the project"),
				15, 314, 610, plugin->addToProject);

			controls->note = CreateLabel(hwnd, L"", 15, 346, 610, 36, AVS::LabelType::Disabled,
				DT_LEFT | DT_TOP | DT_WORDBREAK);
			controls->output = CreateLabel(hwnd, L"", 15, 384, 610, 22, AVS::LabelType::Disabled,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			controls->progress = AVS::CreateProgressBar(hwnd, instance, 15, 412, 610, 20,
				AVS::ProgressBarSettings::Create());
			AVS::ProgressBar_SetRange(controls->progress, 0, 100);
			controls->status = CreateLabel(hwnd, Translate(L"Ready"), 15, 438, 610, 40,
				AVS::LabelType::Enabled, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

			controls->openFolder = AVS::CreateButton(hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OPEN_FOLDER)),
				instance, Translate(L"Show folder").c_str(), 15, 482, 150, 30, defaultButton);
			controls->action = AVS::CreateButton(hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ACTION)),
				instance, Translate(L"Change voice").c_str(), 445, 482, 180, 30,
				AVS::ButtonSettings::Create(AVS::Buttons::Primary));
			EnableWindow(controls->openFolder, FALSE);
			SetBusy(controls, false);
			ShowFiles(plugin, controls);
			ShowNote(plugin, controls);
			plugin->window = hwnd;
			return 0;
		}

		case WM_COMMAND:
			if (!plugin || !controls)
				break;
			switch (LOWORD(wParam)) {
			case ID_BROWSE:
				if (!plugin->busy && ChooseFile(hwnd, controls->input)) {
					ShowFiles(plugin, controls);
					AVS::Label_SetText(controls->status, Translate(L"Ready"));
				}
				return 0;
			case ID_VOICE: {
				std::filesystem::path voice = plugin->voice;
				if (!plugin->busy && ChooseFile(hwnd, voice)) {
					plugin->voice = voice;
					plugin->SaveSettings();
					ShowFiles(plugin, controls);
				}
				return 0;
			}
			case ID_FORMAT:
				if (HIWORD(wParam) == CBN_SELCHANGE) {
					ReadChoices(plugin, controls);
					ShowFiles(plugin, controls);
				}
				return 0;
			case ID_OPEN_FOLDER:
				if (!controls->job.output.empty())
					ShellExecuteW(hwnd, L"open", L"explorer.exe",
						(L"/select,\"" + controls->job.output.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
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

		case WM_VOICE_STATUS:
			if (controls) {
				std::unique_ptr<UiStatus> status(reinterpret_cast<UiStatus*>(lParam));
				AVS::Label_SetText(controls->status, status->text);
				if (status->progress >= 0)
					AVS::ProgressBar_SetPos(controls->progress, status->progress);
				if (status->finished) {
					EnableWindow(controls->action, TRUE);
					SetBusy(controls, false);
					ShowNote(plugin, controls);
					if (status->success) {
						EnableWindow(controls->openFolder, TRUE);
						AddToProject(plugin, controls);
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
			if (plugin && controls) {
				ReadChoices(plugin, controls);
				plugin->SaveSettings();
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

void ShowVoiceCloneWindow(CVoiceClonePlugin* plugin, const std::filesystem::path& initialFile) {
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

	// common controls v6 from the module manifest, for the themed checkboxes
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
		windowClass.hIcon = static_cast<HICON>(LoadImageW(nullptr, icon.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
		windowClass.hIconSm = static_cast<HICON>(LoadImageW(nullptr, icon.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE));
		const AVS::Color background = AVS::Color::GetDefaultWindowBackground();
		windowClass.hbrBackground = CreateSolidBrush(RGB(background.R, background.G, background.B));
		windowClass.lpszClassName = WINDOW_CLASS;
		RegisterClassExW(&windowClass);
	}

	const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	RECT rect{ 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
	AdjustWindowRectEx(&rect, style, FALSE, 0);
	HWND hwnd = CreateWindowExW(0, WINDOW_CLASS, Translate(TITLE).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top, host, nullptr, GetModuleHandleW(nullptr), plugin);
	if (hwnd) {
		std::error_code error;
		auto* controls = reinterpret_cast<Controls*>(GetPropW(hwnd, CONTROLS_PROPERTY));
		if (controls && !initialFile.empty() && std::filesystem::is_regular_file(initialFile, error)) {
			controls->input = initialFile;
			ShowFiles(plugin, controls);
			AVS::Label_SetText(controls->status, Translate(L"Ready"));
		}
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

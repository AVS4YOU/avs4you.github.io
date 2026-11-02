#include "ui.h"
#include "../../../sdk/ui/winapi/ui.h"
#include "export_utils.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <memory>
#include <shellapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")

namespace {
	constexpr wchar_t WINDOW_CLASS[] = L"StemSplitterMainWindowClass";
	constexpr wchar_t CONTROLS_PROPERTY[] = L"StemSplitter.Controls";
	constexpr wchar_t TITLE[] = L"Stem Splitter";
	constexpr wchar_t PLUGIN_ID[] = L"StemSplitter.plugin";
	constexpr int CLIENT_WIDTH = 640;
	constexpr int CLIENT_HEIGHT = 440;

	constexpr int ID_BROWSE = 101;
	constexpr int ID_MODE = 102;
	constexpr int ID_FORMAT = 103;
	constexpr int ID_ADD = 104;
	constexpr int ID_OPEN_FOLDER = 105;
	constexpr int ID_ACTION = 106;
	constexpr int ID_TEMP_CHANGE = 107;
	constexpr int ID_TEMP_DEFAULT = 108;

	struct Controls {
		HWND file = nullptr;
		HWND browse = nullptr;
		HWND mode = nullptr;
		HWND format = nullptr;
		HWND add = nullptr;
		HWND temp = nullptr;
		HWND tempChange = nullptr;
		HWND tempDefault = nullptr;
		HWND output = nullptr;
		HWND progress = nullptr;
		HWND status = nullptr;
		HWND openFolder = nullptr;
		HWND action = nullptr;
		std::filesystem::path input;
		StemJob job;                 // the running or last job
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

	void SetBusy(Controls* controls, bool busy) {
		ShowWindow(controls->progress, busy ? SW_SHOW : SW_HIDE);
		if (busy)
			AVS::ProgressBar_SetPos(controls->progress, 0);
		for (HWND input : { controls->browse, controls->mode, controls->format, controls->add,
			controls->tempChange, controls->tempDefault })
			EnableWindow(input, !busy);
		if (busy)
			EnableWindow(controls->openFolder, FALSE);
		AVS::Button_SetSettings(controls->action,
			busy ? CancelButtonSettings() : AVS::ButtonSettings::Create(AVS::Buttons::Primary),
			Translate(busy ? L"Cancel" : L"Separate"));
	}

	void ShowInput(Controls* controls) {
		AVS::Label_SetText(controls->file,
			controls->input.empty() ? Translate(L"No file selected") : controls->input.wstring());
		AVS::Label_SetText(controls->output, controls->input.empty() ? std::wstring()
			: Translate(L"Tracks will be saved to the folder") + L" \"" +
			DefaultStemFolder(controls->input).filename().wstring() + L"\"");
	}

	void ShowTempFolder(const CStemSplitterPlugin* plugin, Controls* controls) {
		AVS::Label_SetText(controls->temp, plugin->tempDirectory.empty()
			? (plugin->workDirectory / L"tmp").wstring() + L"  (" + Translate(L"Default") + L")"
			: plugin->tempDirectory.wstring());
	}

	// the Explorer folder picker
	bool ChooseFolder(HWND owner, const std::wstring& title, std::filesystem::path& folder) {
		const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		bool chosen = false;
		IFileOpenDialog* dialog = nullptr;
		if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&dialog)))) {
			DWORD options = 0;
			dialog->GetOptions(&options);
			dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
			dialog->SetTitle(title.c_str());
			IShellItem* start = nullptr;
			if (!folder.empty() &&
				SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
				dialog->SetFolder(start);
				start->Release();
			}
			IShellItem* item = nullptr;
			if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item))) {
				wchar_t* path = nullptr;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
					folder = path;
					CoTaskMemFree(path);
					chosen = true;
				}
				item->Release();
			}
			dialog->Release();
		}
		if (SUCCEEDED(com))
			CoUninitialize();
		return chosen;
	}

	bool ChooseFile(HWND owner, Controls* controls) {
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
		controls->input = path;
		return true;
	}

	void Start(HWND hwnd, CStemSplitterPlugin* plugin, Controls* controls) {
		if (controls->input.empty()) {
			AVS::Label_SetText(controls->status, Translate(L"Choose a music or video file first."));
			return;
		}
		const auto& models = StemModels();
		const auto& formats = StemFormats();
		const int mode = AVS::ComboBox_GetCurrent(controls->mode);
		const int format = AVS::ComboBox_GetCurrent(controls->format);
		plugin->model = models[(std::max)(0, (std::min)(mode, (int)models.size() - 1))].id;
		plugin->format = formats[(std::max)(0, (std::min)(format, (int)formats.size() - 1))].id;
		plugin->addToProject = SendMessageW(controls->add, BM_GETCHECK, 0, 0) == BST_CHECKED;
		plugin->SaveSettings();

		StemJob job;
		job.input = controls->input;
		job.outputDirectory = DefaultStemFolder(job.input);
		job.model = plugin->model;
		job.format = plugin->format;
		std::wstring existing;
		std::error_code error;
		for (const auto& output : StemOutputs(plugin->Tools(), job))
			if (std::filesystem::exists(output, error))
				existing += L"\r\n" + output.filename().wstring();
		if (!existing.empty()) {
			const std::wstring question = Translate(L"These files already exist. Replace them?") +
				L"\r\n" + existing;
			if (MessageBoxW(hwnd, question.c_str(), Translate(TITLE).c_str(),
				MB_YESNO | MB_ICONQUESTION) != IDYES)
				return;
		}

		controls->job = job;
		SetBusy(controls, true);
		AVS::Label_SetText(controls->status, Translate(L"Starting..."));
		plugin->Start(std::move(job));
	}

	// the tracks into the host's media library, as the other content plugins
	// hand over what they made
	void AddToProject(CStemSplitterPlugin* plugin, const Controls* controls) {
		if (!plugin->addToProject || !plugin->callback)
			return;
		for (const auto& output : StemOutputs(plugin->Tools(), controls->job))
			plugin->callback(export_str(PLUGIN_ID), export_str(output.c_str()), 0,
				plugin->callbackContext);
	}

	LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
		auto* plugin = reinterpret_cast<CStemSplitterPlugin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
			plugin = reinterpret_cast<CStemSplitterPlugin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			controls = new Controls();
			SetPropW(hwnd, CONTROLS_PROPERTY, controls);
			HINSTANCE instance = GetModuleHandleW(nullptr);

			CreateLabel(hwnd, Translate(L"Song or video"), 15, 12, 400, 22);
			controls->file = CreateLabel(hwnd, L"", 15, 36, 480, 30, AVS::LabelType::Enabled,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
			controls->browse = AVS::CreateButton(hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BROWSE)), instance,
				Translate(L"Choose file...").c_str(), 505, 36, 120, 30,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));

			CreateLabel(hwnd, Translate(L"Separate into"), 15, 84, 380, 22);
			CreateLabel(hwnd, Translate(L"Format"), 410, 84, 215, 22);
			std::vector<std::wstring> modeItems;
			int modeIndex = 0;
			const auto& models = StemModels();
			for (size_t index = 0; index < models.size(); ++index) {
				modeItems.push_back(Translate(models[index].label));
				if (plugin->model == models[index].id)
					modeIndex = static_cast<int>(index);
			}
			AVS::ComboBoxSettings modeSettings = AVS::ComboBoxSettings::Create();
			modeSettings.StartSelectedIndex = modeIndex;
			controls->mode = AVS::CreateComboBox(hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_MODE)), instance, 15, 108, 380, 28,
				modeSettings, modeItems);

			std::vector<std::wstring> formatItems;
			int formatIndex = 0;
			const auto& formats = StemFormats();
			for (size_t index = 0; index < formats.size(); ++index) {
				formatItems.push_back(formats[index].label);
				if (plugin->format == formats[index].id)
					formatIndex = static_cast<int>(index);
			}
			AVS::ComboBoxSettings formatSettings = AVS::ComboBoxSettings::Create();
			formatSettings.StartSelectedIndex = formatIndex;
			controls->format = AVS::CreateComboBox(hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FORMAT)), instance, 410, 108, 215, 28,
				formatSettings, formatItems);

			controls->add = CreateCheckBox(hwnd, ID_ADD, Translate(L"Add the tracks to the project"),
				15, 152, 400, plugin->addToProject);
			CreateLabel(hwnd, Translate(L"Temporary files folder"), 15, 184, 400, 22);
			controls->temp = CreateLabel(hwnd, L"", 15, 208, 330, 30, AVS::LabelType::Enabled,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
			controls->tempChange = AVS::CreateButton(hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TEMP_CHANGE)), instance,
				Translate(L"Change...").c_str(), 355, 208, 130, 30,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));
			controls->tempDefault = AVS::CreateButton(hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TEMP_DEFAULT)), instance,
				Translate(L"Default").c_str(), 495, 208, 130, 30,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));
			ShowTempFolder(plugin, controls);

			CreateLabel(hwnd,
				Translate(L"The separation model is downloaded once, the first time it is used. "
					L"Everything runs on this computer - nothing is uploaded."),
				15, 244, 610, 40, AVS::LabelType::Disabled, DT_LEFT | DT_TOP | DT_WORDBREAK);
			controls->output = CreateLabel(hwnd, L"", 15, 288, 610, 22, AVS::LabelType::Disabled,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

			controls->progress = AVS::CreateProgressBar(hwnd, instance, 15, 318, 610, 20,
				AVS::ProgressBarSettings::Create());
			AVS::ProgressBar_SetRange(controls->progress, 0, 100);
			controls->status = CreateLabel(hwnd, Translate(L"Ready"), 15, 344, 610, 40,
				AVS::LabelType::Enabled, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

			controls->openFolder = AVS::CreateButton(hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OPEN_FOLDER)), instance,
				Translate(L"Show folder").c_str(), 15, 396, 150, 30,
				AVS::ButtonSettings::Create(AVS::Buttons::Default));
			controls->action = AVS::CreateButton(hwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ACTION)), instance,
				Translate(L"Separate").c_str(), 445, 396, 180, 30,
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
			case ID_TEMP_CHANGE: {
				std::filesystem::path folder = plugin->tempDirectory.empty()
					? plugin->workDirectory / L"tmp" : plugin->tempDirectory;
				if (!plugin->busy && ChooseFolder(hwnd, Translate(L"Choose a folder for temporary files"),
					folder)) {
					plugin->tempDirectory = folder;
					plugin->SaveSettings();
					ShowTempFolder(plugin, controls);
				}
				return 0;
			}
			case ID_TEMP_DEFAULT:
				if (!plugin->busy) {
					plugin->tempDirectory.clear();
					plugin->SaveSettings();
					ShowTempFolder(plugin, controls);
				}
				return 0;
			case ID_OPEN_FOLDER:
				if (!controls->job.outputDirectory.empty())
					ShellExecuteW(hwnd, L"open", controls->job.outputDirectory.c_str(), nullptr, nullptr,
						SW_SHOWNORMAL);
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

		case WM_STEMS_STATUS:
			if (controls) {
				std::unique_ptr<UiStatus> status(reinterpret_cast<UiStatus*>(lParam));
				AVS::Label_SetText(controls->status, status->text);
				if (status->progress >= 0)
					AVS::ProgressBar_SetPos(controls->progress, status->progress);
				if (status->finished) {
					EnableWindow(controls->action, TRUE);
					SetBusy(controls, false);
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

void ShowStemSplitterWindow(CStemSplitterPlugin* plugin, const std::filesystem::path& initialFile) {
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
	HWND hwnd = CreateWindowExW(0, WINDOW_CLASS, Translate(TITLE).c_str(), style, CW_USEDEFAULT,
		CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, host, nullptr,
		GetModuleHandleW(nullptr), plugin);
	if (hwnd) {
		std::error_code error;
		auto* controls = reinterpret_cast<Controls*>(GetPropW(hwnd, CONTROLS_PROPERTY));
		if (controls && !initialFile.empty() && std::filesystem::is_regular_file(initialFile, error)) {
			controls->input = initialFile;
			ShowInput(controls);
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

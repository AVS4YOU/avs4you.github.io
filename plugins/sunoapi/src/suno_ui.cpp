#include "suno_ui.h"
#include "../../../sdk/3dparty/nlohmann/json/single_include/nlohmann/json.hpp"
#include "../../../sdk/ui/winapi/ui.h"
#include "../../../sdk/translate/translate.h"
#include "export_utils.h"
#include "sha256.h"
#include "suno_api.h"
#include <atomic>
#include <commctrl.h>
#include <fstream>
#include <shellapi.h>
#include <thread>
#pragma comment(lib, "comctl32.lib")

using json = nlohmann::json;

#define SUNO_MODE_GENERATE_MUSIC 0
#define SUNO_MODE_EXTEND_MUSIC 1
#define SUNO_MODE_GENERATE_LYRICS 2
#define SUNO_MODE_GENERATE_SOUNDS 3
#define SUNO_MODE_COVER_AUDIO 4
#define SUNO_MODE_ADD_VOCALS 5

#define SUNO_API_SUCCESS_CODE 200
#define SUNO_HTTP_SUCCESS_MIN 200
#define SUNO_HTTP_SUCCESS_MAX 300
#define SUNO_TASK_POLL_ATTEMPTS 90
#define SUNO_TASK_POLL_INTERVAL_MS 4000
#define SUNO_DOWNLOAD_ATTEMPTS 3
#define SUNO_DOWNLOAD_RETRY_INTERVAL_MS 750
#define SUNO_HELP_URL L"https://docs.sunoapi.org/#-music-generation-apis"
#define SUNO_CALLBACK_URL "https://api.example.com/callback"
#define SUNO_PROMPT_LABEL_X 16
#define SUNO_PROMPT_X 130
#define SUNO_PROMPT_COUNTER_X 550
#define SUNO_PROMPT_WIDTH 520
#define SUNO_PROMPT_DEFAULT_COUNTER_Y 133
#define SUNO_PROMPT_DEFAULT_LABEL_Y 159
#define SUNO_PROMPT_DEFAULT_Y 155
#define SUNO_PROMPT_DEFAULT_HEIGHT 120
#define SUNO_PROMPT_EXPANDED_COUNTER_Y 81
#define SUNO_PROMPT_EXPANDED_LABEL_Y 106
#define SUNO_PROMPT_EXPANDED_Y 102
#define SUNO_PROMPT_EXPANDED_HEIGHT 266

namespace
{
bool ReleaseActivationContext(CSunoApiPlugin *plugin)
{
	if (plugin->activationContext == INVALID_HANDLE_VALUE)
		return true;
	if (plugin->activationCookie)
	{
		if (plugin->activationThreadId != GetCurrentThreadId() || !DeactivateActCtx(0, plugin->activationCookie))
			return false;
	}
	ReleaseActCtx(plugin->activationContext);
	plugin->activationContext = INVALID_HANDLE_VALUE;
	plugin->activationCookie = 0;
	plugin->activationThreadId = 0;
	return true;
}
std::wstring Tr(const wchar_t *text)
{
	return CTranslate::GetInstance().GetManager()->Translate(text);
}

void SetWindowClassIcons(WNDCLASSEXW &windowClass, CSunoApiPlugin *plugin)
{
	const std::wstring iconPath = plugin->workDirectory + L"\\icon_internal.ico";
	windowClass.hIcon = (HICON)LoadImageW(g_hInst, iconPath.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
	windowClass.hIconSm = (HICON)LoadImageW(g_hInst, iconPath.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
}

enum
{
	ID_MODE = 100,
	ID_MODEL,
	ID_PROMPT,
	ID_STYLE,
	ID_NEGATIVE_TAGS,
	ID_VOCAL_GENDER,
	ID_TITLE,
	ID_SOURCE,
	ID_FILE,
	ID_CUSTOM,
	ID_INSTRUMENTAL,
	ID_HELP,
	ID_RUN,
	ID_SETTINGS,
	ID_STATUS,
	ID_LYRICS_SELECT_FIRST,
	ID_LYRICS_SELECT_SECOND
};

struct State
{
	CSunoApiPlugin *p = nullptr;
	HWND mode{}, model{}, promptLabel{}, prompt{}, styleLabel{}, style{}, negativeTagsLabel{}, negativeTags{}, vocalGenderLabel{}, vocalGender{}, titleLabel{}, title{}, sourceLabel{}, source{}, titleCounter{}, promptCounter{}, styleCounter{}, custom{}, instrumental{}, help{}, file{}, settings{}, run{}, status{};
	std::atomic<bool> busy{false};
	bool customEnabled = false;
	bool instrumentalEnabled = false;
};

std::wstring Text(HWND h)
{
	int n = GetWindowTextLengthW(h);
	std::wstring s(n, 0);
	if (n)
		GetWindowTextW(h, &s[0], n + 1);
	return s;
}

void Set(HWND h, const std::wstring &s)
{
	SetWindowTextW(h, s.c_str());
}

std::wstring NormalizeLineEndings(const std::wstring &text)
{
	std::wstring result;
	result.reserve(text.size() + 16);
	for (size_t index = 0; index < text.size(); ++index)
	{
		if (text[index] == L'\r')
		{
			result += L"\r\n";
			if (index + 1 < text.size() && text[index + 1] == L'\n')
				++index;
		}
		else if (text[index] == L'\n')
			result += L"\r\n";
		else
			result += text[index];
	}
	return result;
}
HWND Label(HWND w, const wchar_t *s, int x, int y, int width = 110)
{
	return AVS::CreateLabel(w, g_hInst, s, x, y, width, 20, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
}

HWND Edit(HWND w, int id, int x, int y, int width, int height, bool multi = false)
{
	(void)id;
	AVS::TextEditSettings settings = AVS::TextEditSettings::Create();
	settings.IsMultiline = multi;
	settings.IsVscroll = multi;
	return AVS::CreateTextEditMultiline(w, g_hInst, x, y, width, height, settings);
}

void Status(State *s, const std::wstring &text)
{
	PostMessageW(s->p->window, WM_APP + 1, 0, (LPARAM) new std::wstring(text));
}

std::wstring SanitizeFileName(std::wstring value)
{
	const std::wstring invalid = L"<>:\"/\\|?*";
	
	for (wchar_t &character : value)
	{
		if (character < 32 || invalid.find(character) != std::wstring::npos)
			character = L'_';
	}
	while (!value.empty() && (value.back() == L' ' || value.back() == L'.'))
		value.pop_back();
	
	size_t first = value.find_first_not_of(L" .");
	
	if (first == std::wstring::npos)
		return {};
	
	if (first > 0)
		value.erase(0, first);

	static const std::vector<std::wstring> reserved = {L"CON",
		L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3", L"COM4",
		L"COM5", L"COM6", L"COM7", L"COM8", L"COM9", L"LPT1", L"LPT2",
		L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"};
	
	std::wstring upper = value;
	
	for (wchar_t &character : upper)
		character = static_cast<wchar_t>(towupper(character));
	
	for (const auto &name : reserved)
	{
		if (upper == name)
		{
			value.insert(value.begin(), L'_');
			break;
		}
	}
	return value;
}

bool NotifyHost(State *state, const std::wstring &path)
{
	if (!state->p->callback)
		return false;
	
	auto *message = new std::wstring(path);
	
	if (!PostMessageW(state->p->window, WM_APP + 5, 0, reinterpret_cast<LPARAM>(message)))
	{
		delete message;
		return false;
	}
	return true;
}

std::wstring Endpoint(int mode)
{
	switch (mode)
	{
	case SUNO_MODE_EXTEND_MUSIC:
		return L"/api/v1/generate/extend";
	case SUNO_MODE_GENERATE_LYRICS:
		return L"/api/v1/lyrics";
	case SUNO_MODE_GENERATE_SOUNDS:
		return L"/api/v1/generate/sounds";
	case SUNO_MODE_COVER_AUDIO:
		return L"/api/v1/generate/upload-cover";
	case SUNO_MODE_ADD_VOCALS:
		return L"/api/v1/generate/add-vocals";
	default:
		return L"/api/v1/generate";
	}
}

std::wstring RecordEndpoint(int mode, const std::string &id)
{
	if (mode == SUNO_MODE_GENERATE_LYRICS)
		return L"/api/v1/lyrics/record-info?taskId=" + Suno::Utf8ToWide(id);
	return L"/api/v1/generate/record-info?taskId=" + Suno::Utf8ToWide(id);
}

std::wstring CachePath(const State *state)
{
	return state->p->workDirectory + L"\\cache.json";
}

bool FindTrackInCache(State *state, const std::string &hash, std::wstring &audioId, std::wstring &audioUrl)
{
	try
	{
		std::ifstream input(CachePath(state), std::ios::binary);
		if (!input.is_open())
			return false;
		json cache;
		input >> cache;
		if (!cache.is_object() || !cache.contains(hash))
			return false;
		const json &entry = cache[hash];
		if (entry.is_string())
		{
			audioId = Suno::Utf8ToWide(entry.get<std::string>());
			return true;
		}
		if (entry.is_object())
		{
			audioId = Suno::Utf8ToWide(entry.value("audioId", ""));
			audioUrl = Suno::Utf8ToWide(entry.value("audioUrl", ""));
			return true;
		}
		return false;
	}
	catch (...)
	{
		return false;
	}
}

void SaveTrackToCache(State *state, const std::wstring &filePath, const std::string &taskId, const std::string &audioId, const std::string &audioUrl)
{
	if (audioUrl.empty())
		return;
	try
	{
		const std::string hash = CalcFileSHA256(filePath);
		const std::wstring path = CachePath(state);
		json cache = json::object();
		std::ifstream input(path, std::ios::binary);
		if (input.is_open())
		{
			try
			{
				input >> cache;
				if (!cache.is_object())
					cache = json::object();
			}
			catch (...)
			{
				cache = json::object();
			}
		}
		cache[hash] = {{"taskId", taskId}, {"audioId", audioId}, {"audioUrl", audioUrl}};
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (output.is_open())
			output << cache.dump(4);
	}
	catch (...)
	{
	}
}

struct LyricsVariant
{
	std::wstring text;
	std::wstring title;
};

struct GeneratedLyrics
{
	std::vector<LyricsVariant> variants;
};
struct InputLimits
{
	int title = 0;
	int prompt = 0;
	int style = 0;
};

InputLimits GetInputLimits(State *state)
{
	const int mode = AVS::ComboBox_GetCurrent(state->mode);
	const std::wstring model = AVS::ComboBox_GetCurrentText(state->model);
	const bool isV4 = model == L"V4";
	const bool shortTitle = isV4 || model == L"V4_5ALL";

	if (mode == SUNO_MODE_GENERATE_LYRICS)
		return {0, 200, 0};
	if (mode == SUNO_MODE_GENERATE_SOUNDS)
		return {0, 500, 0};
	if (mode == SUNO_MODE_ADD_VOCALS)
		return {100, 0, 0};
	if (mode == SUNO_MODE_EXTEND_MUSIC || ((mode == SUNO_MODE_GENERATE_MUSIC || mode == SUNO_MODE_COVER_AUDIO) && state->customEnabled))
		return {shortTitle ? 80 : 100, isV4 ? 3000 : 5000, isV4 ? 200 : 1000};
	if (mode == SUNO_MODE_GENERATE_MUSIC || mode == SUNO_MODE_COVER_AUDIO)
		return {0, 500, 0};
	return {};
}

void SetCounter(HWND counter, HWND edit, int limit)
{
	const int length = GetWindowTextLengthW(edit);
	ShowWindow(counter, limit > 0 ? SW_SHOW : SW_HIDE);
	if (limit > 0)
		AVS::Label_SetText(counter, std::to_wstring(length) + L"/" + std::to_wstring(limit));
	SendMessageW(edit, EM_SETLIMITTEXT, limit > 0 ? static_cast<WPARAM>(limit) : 0x7FFFFFFE, 0);
}
void UpdateCounters(State *state)
{
	const InputLimits limits = GetInputLimits(state);
	SetCounter(state->titleCounter, state->title, limits.title);
	SetCounter(state->promptCounter, state->prompt, limits.prompt);
	SetCounter(state->styleCounter, state->style, limits.style);
}

void UpdateSourceField(State *state)
{
	int mode = AVS::ComboBox_GetCurrent(state->mode);
	const bool generateMusicMode = mode == SUNO_MODE_GENERATE_MUSIC;
	const bool generationSwitchesEnabled = generateMusicMode || mode == SUNO_MODE_COVER_AUDIO;
	AVS::Label_SetText(state->promptLabel, generateMusicMode ? (state->customEnabled ? Tr(L"Lyrics") : Tr(L"Prompt")) : Tr(L"Prompt / lyrics"));
	ShowWindow(state->custom, generationSwitchesEnabled ? SW_SHOW : SW_HIDE);
	ShowWindow(state->instrumental, generationSwitchesEnabled ? SW_SHOW : SW_HIDE);
	ShowWindow(state->vocalGenderLabel, generateMusicMode ? SW_SHOW : SW_HIDE);
	ShowWindow(state->vocalGender, generateMusicMode ? SW_SHOW : SW_HIDE);
	const bool addVocalsMode = mode == SUNO_MODE_ADD_VOCALS;
	const bool lyricsMode = mode == SUNO_MODE_GENERATE_LYRICS;
	const bool soundMode = mode == SUNO_MODE_GENERATE_SOUNDS;
	const bool showTitleAndStyle = !lyricsMode && !soundMode;
	ShowWindow(state->titleLabel, showTitleAndStyle ? SW_SHOW : SW_HIDE);
	ShowWindow(state->title, showTitleAndStyle ? SW_SHOW : SW_HIDE);
	ShowWindow(state->titleCounter, showTitleAndStyle ? SW_SHOW : SW_HIDE);
	ShowWindow(state->styleLabel, showTitleAndStyle ? SW_SHOW : SW_HIDE);
	ShowWindow(state->style, showTitleAndStyle ? SW_SHOW : SW_HIDE);
	ShowWindow(state->styleCounter, showTitleAndStyle ? SW_SHOW : SW_HIDE);
	SetWindowPos(state->promptCounter, nullptr, SUNO_PROMPT_COUNTER_X, showTitleAndStyle ? SUNO_PROMPT_DEFAULT_COUNTER_Y : SUNO_PROMPT_EXPANDED_COUNTER_Y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	SetWindowPos(state->promptLabel, nullptr, SUNO_PROMPT_LABEL_X, showTitleAndStyle ? SUNO_PROMPT_DEFAULT_LABEL_Y : SUNO_PROMPT_EXPANDED_LABEL_Y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	SetWindowPos(state->prompt, nullptr, SUNO_PROMPT_X, showTitleAndStyle ? SUNO_PROMPT_DEFAULT_Y : SUNO_PROMPT_EXPANDED_Y, SUNO_PROMPT_WIDTH, showTitleAndStyle ? SUNO_PROMPT_DEFAULT_HEIGHT : SUNO_PROMPT_EXPANDED_HEIGHT, SWP_NOZORDER | SWP_NOACTIVATE);
	bool visible = mode == SUNO_MODE_EXTEND_MUSIC || mode == SUNO_MODE_COVER_AUDIO || mode == SUNO_MODE_ADD_VOCALS;
	if (mode == SUNO_MODE_COVER_AUDIO || mode == SUNO_MODE_ADD_VOCALS)
		AVS::Label_SetText(state->sourceLabel, Tr(L"URL"));

	const bool fileMode = mode == SUNO_MODE_EXTEND_MUSIC || mode == SUNO_MODE_COVER_AUDIO || mode == SUNO_MODE_ADD_VOCALS;
	ShowWindow(state->sourceLabel, visible && !fileMode ? SW_SHOW : SW_HIDE);
	ShowWindow(state->source, visible ? SW_SHOW : SW_HIDE);
	ShowWindow(state->negativeTagsLabel, addVocalsMode ? SW_SHOW : SW_HIDE);
	ShowWindow(state->negativeTags, addVocalsMode ? SW_SHOW : SW_HIDE);
	SetWindowPos(state->source, nullptr, 130, addVocalsMode ? 381 : 343, 520, 25, SWP_NOZORDER | SWP_NOACTIVATE);

	ShowWindow(state->file, fileMode ? SW_SHOW : SW_HIDE);
	SetWindowPos(state->file, nullptr, 16, addVocalsMode ? 381 : 343, 100, 25, SWP_NOZORDER | SWP_NOACTIVATE);
	if (!visible)
		Set(state->source, L"");
	if (!addVocalsMode)
		Set(state->negativeTags, L"");
}

void CenterWindow(HWND hwnd)
{
	RECT windowRect{};
	GetWindowRect(hwnd, &windowRect);

	HWND owner = GetWindow(hwnd, GW_OWNER);
	RECT centerRect{};
	if (owner && IsWindow(owner))
		GetWindowRect(owner, &centerRect);
	else
	{
		HMONITOR centerMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO centerInfo{sizeof(centerInfo)};
		GetMonitorInfoW(centerMonitor, &centerInfo);
		centerRect = centerInfo.rcWork;
	}

	HMONITOR monitor = MonitorFromRect(&centerRect, MONITOR_DEFAULTTONEAREST);
	MONITORINFO info{sizeof(info)};
	GetMonitorInfoW(monitor, &info);
	int width = windowRect.right - windowRect.left;
	int height = windowRect.bottom - windowRect.top;
	int x = centerRect.left + (centerRect.right - centerRect.left - width) / 2;
	int y = centerRect.top + (centerRect.bottom - centerRect.top - height) / 2;
	x = max(info.rcWork.left, min(x, info.rcWork.right - width));
	y = max(info.rcWork.top, min(y, info.rcWork.bottom - height));
	SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}
struct LyricsSelectionState
{
	State *mainState = nullptr;
	GeneratedLyrics *result = nullptr;
	HWND firstText = nullptr;
	HWND secondText = nullptr;
};

void ApplyLyricsVariant(LyricsSelectionState *selection, size_t index)
{
	if (!selection || !selection->mainState || !selection->result || index >= selection->result->variants.size())
		return;

	State *state = selection->mainState;
	const LyricsVariant &variant = selection->result->variants[index];
	const std::wstring normalizedLyrics = NormalizeLineEndings(variant.text);

	AVS::ComboBox_SetCurrent(state->mode, SUNO_MODE_GENERATE_MUSIC);
	state->customEnabled = true;
	state->instrumentalEnabled = false;
	AVS::Button_SetSettings(state->custom, AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupEnable), Tr(L"Custom mode"));
	AVS::Button_SetSettings(state->instrumental, AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable), Tr(L"Instrumental"));
	UpdateSourceField(state);
	UpdateCounters(state);

	SendMessageW(state->prompt, EM_SETLIMITTEXT, normalizedLyrics.size() > 5000 ? static_cast<WPARAM>(normalizedLyrics.size()) : 5000, 0);
	Set(state->prompt, normalizedLyrics);
	Set(state->title, variant.title);
	UpdateCounters(state);
	AVS::Label_SetText(state->status, Tr(L"Lyrics selected. Generate music Custom mode enabled."));
	SetFocus(state->prompt);
}

LRESULT CALLBACK LyricsSelectionProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	LyricsSelectionState *state = reinterpret_cast<LyricsSelectionState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		state = reinterpret_cast<LyricsSelectionState *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
	}
	if (!state)
		return DefWindowProcW(hwnd, message, wParam, lParam);

	switch (message)
	{
	case WM_CREATE: {
		AVS::CreateLabel(hwnd, g_hInst, Tr(L"Choose one of the two generated lyrics").c_str(), 15, 15, 735, 20, AVS::LabelSettings::Create(AVS::LabelType::Enabled));
		const LyricsVariant &first = state->result->variants[0];
		const std::wstring firstTitle = first.title.empty() ? Tr(L"Variant 1") : L"1. " + first.title;
		AVS::CreateLabel(hwnd, g_hInst, firstTitle.c_str(), 15, 45, 355, 20, AVS::LabelSettings::Create(AVS::LabelType::Enabled));
		state->firstText = Edit(hwnd, 0, 15, 70, 355, 330, true);
		Set(state->firstText, NormalizeLineEndings(first.text));
		SendMessageW(state->firstText, EM_SETREADONLY, TRUE, 0);
		AVS::CreateButton(hwnd, (HMENU)ID_LYRICS_SELECT_FIRST, g_hInst, Tr(L"Select first").c_str(), 142, 415, 105, 30, AVS::ButtonSettings::Create(AVS::Buttons::Primary));

		LyricsVariant second;
		if (state->result->variants.size() > 1)
			second = state->result->variants[1];
		const std::wstring secondTitle = second.title.empty() ? Tr(L"Variant 2") : L"2. " + second.title;
		AVS::CreateLabel(hwnd, g_hInst, secondTitle.c_str(), 400, 45, 355, 20, AVS::LabelSettings::Create(AVS::LabelType::Enabled));
		state->secondText = Edit(hwnd, 0, 400, 70, 355, 330, true);
		Set(state->secondText, NormalizeLineEndings(second.text));
		SendMessageW(state->secondText, EM_SETREADONLY, TRUE, 0);
		HWND secondButton = AVS::CreateButton(hwnd, (HMENU)ID_LYRICS_SELECT_SECOND, g_hInst, Tr(L"Select second").c_str(), 527, 415, 105, 30, AVS::ButtonSettings::Create(AVS::Buttons::Primary));
		if (state->result->variants.size() < 2)
			EnableWindow(secondButton, FALSE);
		return 0;
	}
	case WM_COMMAND:
		if (LOWORD(wParam) == ID_LYRICS_SELECT_FIRST || LOWORD(wParam) == ID_LYRICS_SELECT_SECOND)
		{
			ApplyLyricsVariant(state, LOWORD(wParam) == ID_LYRICS_SELECT_FIRST ? 0 : 1);
			DestroyWindow(hwnd);
			return 0;
		}
		break;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_NCDESTROY: {
		HWND owner = GetWindow(hwnd, GW_OWNER);
		if (owner)
		{
			EnableWindow(owner, TRUE);
			SetForegroundWindow(owner);
		}
		delete state->result;
		delete state;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		break;
	}
	}
	return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ShowLyricsSelection(HWND owner, State *mainState, GeneratedLyrics *result)
{
	const wchar_t *className = L"SunoApiLyricsSelectionWindowClass";
	WNDCLASSEXW windowClass{sizeof(windowClass)};
	if (!GetClassInfoExW(g_hInst, className, &windowClass))
	{
		windowClass.lpfnWndProc = LyricsSelectionProc;
		windowClass.hInstance = g_hInst;
		windowClass.lpszClassName = className;
		windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
		SetWindowClassIcons(windowClass, mainState->p);
		AVS::Color background = AVS::Color::GetDefaultWindowBackground();
		windowClass.hbrBackground = CreateSolidBrush(RGB(background.R, background.G, background.B));
		RegisterClassExW(&windowClass);
	}

	auto *state = new LyricsSelectionState{mainState, result};
	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
	RECT rect{0, 0, 770, 465};
	AdjustWindowRectEx(&rect, style, FALSE, WS_EX_DLGMODALFRAME);
	HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, className, Tr(L"Choose one of the two generated lyrics").c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, owner, nullptr, g_hInst, state);
	if (!window)
	{
		delete result;
		delete state;
		return;
	}
	EnableWindow(owner, FALSE);
	CenterWindow(window);
	ShowWindow(window, SW_SHOW);
	UpdateWindow(window);
}
LRESULT CALLBACK SettingsProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	struct SettingsState
	{
		CSunoApiPlugin *plugin = nullptr;
		HWND edit = nullptr;
	};

	SettingsState *state = reinterpret_cast<SettingsState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		state = new SettingsState;
		state->plugin = reinterpret_cast<CSunoApiPlugin *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
	}
	if (!state)
		return DefWindowProcW(hwnd, message, wParam, lParam);

	auto closeWindow = [hwnd]() {
		HWND owner = GetWindow(hwnd, GW_OWNER);
		if (owner)
		{
			EnableWindow(owner, TRUE);
			SetForegroundWindow(owner);
		}
		DestroyWindow(hwnd);
	};

	switch (message)
	{
	case WM_CREATE: {
		state->edit = Edit(hwnd, 0, 15, 15, 330, 25);
		SendMessageW(state->edit, EM_SETPASSWORDCHAR, 0x25CF, 0);
		std::wifstream file(state->plugin->workDirectory + L"\\app.key");
		std::wstring key;
		std::getline(file, key);
		Set(state->edit, key);

		AVS::CreateButton(hwnd, (HMENU)0x8201, g_hInst, Tr(L"Save").c_str(), 265, 55, 80, 25, AVS::ButtonSettings::Create(AVS::Buttons::Primary));
		AVS::CreateButton(hwnd, (HMENU)0x8202, g_hInst, Tr(L"Cancel").c_str(), 175, 55, 80, 25, AVS::ButtonSettings::Create(AVS::Buttons::Default));
		AVS::CreateButton(hwnd, (HMENU)0x8203, g_hInst, Tr(L"Delete API key").c_str(), 15, 55, 130, 25, AVS::ButtonSettings::Create(AVS::Buttons::Default));
		SetFocus(state->edit);
		return 0;
	}
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case 0x8201: {
			std::wstring key = Text(state->edit);
			std::wofstream file(state->plugin->workDirectory + L"\\app.key", std::ios::trunc);
			file << key;
			file.close();
			if (state->plugin->window)
				PostMessageW(state->plugin->window, WM_APP + 4, 0, 0);
			closeWindow();
			return 0;
		}
		case 0x8202:
		case IDCANCEL:
			closeWindow();
			return 0;
		case 0x8203:
			DeleteFileW((state->plugin->workDirectory + L"\\app.key").c_str());
			Set(state->edit, L"");
			if (state->plugin->window)
				PostMessageW(state->plugin->window, WM_APP + 4, 0, 0);
			return 0;
		}
		break;
	case WM_CLOSE:
		closeWindow();
		return 0;
	case WM_NCDESTROY:
		delete state;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		break;
	}
	return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ShowSettings(HWND owner, CSunoApiPlugin *plugin)
{
	const wchar_t *className = L"SunoApiSettingsWindowClass";
	WNDCLASSEXW windowClass{sizeof(windowClass)};
	
	if (!GetClassInfoExW(g_hInst, className, &windowClass))
	{
		windowClass.lpfnWndProc = SettingsProc;
		windowClass.hInstance = g_hInst;
		windowClass.lpszClassName = className;
		windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
		SetWindowClassIcons(windowClass, plugin);
		AVS::Color background = AVS::Color::GetDefaultWindowBackground();
		windowClass.hbrBackground = CreateSolidBrush(RGB(background.R, background.G, background.B));
		RegisterClassExW(&windowClass);
	}

	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
	RECT rect{0, 0, 360, 95};
	AdjustWindowRectEx(&rect, style, FALSE, 0);
	
	HWND window = CreateWindowExW(0, className,
									(L"Suno API " + Tr(L"Settings")).c_str(),
									style,
									CW_USEDEFAULT, CW_USEDEFAULT, 
									rect.right - rect.left,
									rect.bottom - rect.top,
									owner,
									nullptr,
									g_hInst,
									plugin);
	EnableWindow(owner, FALSE);
	CenterWindow(window);
	ShowWindow(window, SW_SHOW);
	UpdateWindow(window);
}

void LoadCredits(HWND window, CSunoApiPlugin *plugin)
{
	std::wstring key;
	{
		std::wifstream file(plugin->workDirectory + L"\\app.key");
		std::getline(file, key);
	}

	std::wstring status;
	if (key.empty())
	{
		status = Tr(L"Ready (Credits left: API key required)");
	}
	else
	{
		const auto response = Suno::Request(key, L"GET", L"/api/v1/generate/credit");
		try
		{
			const json body = json::parse(response.body);
			if (response.error.empty() && response.status >= SUNO_HTTP_SUCCESS_MIN && response.status < SUNO_HTTP_SUCCESS_MAX && body.value("code", 0) == SUNO_API_SUCCESS_CODE && body.contains("data") && body["data"].is_number())
			{
				std::string credits = body["data"].dump();
				if (credits.size() > 2 && credits.compare(credits.size() - 2, 2, ".0") == 0)
					credits.resize(credits.size() - 2);
				status = Tr(L"Ready (Credits left: ") + Suno::Utf8ToWide(credits) + L")";
			}
			else
			{
				status = Tr(L"Ready (Credits left: unavailable)");
			}
		}
		catch (...)
		{
			status = Tr(L"Ready (Credits left: unavailable)");
		}
	}

	auto *message = new std::wstring(status);
	if (!PostMessageW(window, WM_APP + 3, 0, reinterpret_cast<LPARAM>(message)))
		delete message;
}

void Worker(State *s)
{
	auto done = [&]() { PostMessageW(s->p->window, WM_APP + 2, 0, 0); };
	std::wstring key;
	{
		std::wifstream f(s->p->workDirectory + L"\\app.key");
		std::getline(f, key);
	}
	if (key.empty())
	{
		Status(s, Tr(L"Open API key settings first."));
		done();
		return;
	}
	int mode = AVS::ComboBox_GetCurrent(s->mode);
	std::wstring prompt = Text(s->prompt), style = Text(s->style), negativeTags = Text(s->negativeTags), title = Text(s->title), source = Text(s->source), model = AVS::ComboBox_GetCurrentText(s->model);
	json b;
	b["prompt"] = Suno::WideToUtf8(prompt);
	b["model"] = Suno::WideToUtf8(model);
	b["callBackUrl"] = SUNO_CALLBACK_URL;
	if (mode == SUNO_MODE_GENERATE_MUSIC)
	{
		b["customMode"] = s->customEnabled;
		b["instrumental"] = s->instrumentalEnabled;
		if (!style.empty())
			b["style"] = Suno::WideToUtf8(style);
		if (!title.empty())
			b["title"] = Suno::WideToUtf8(title);
		if (s->customEnabled && !s->instrumentalEnabled)
			b["vocalGender"] = AVS::ComboBox_GetCurrent(s->vocalGender) == 1 ? "f" : "m";
	}
	else if (mode == SUNO_MODE_EXTEND_MUSIC)
	{
		b["audioId"] = Suno::WideToUtf8(source);
		b["defaultParamFlag"] = true;
		if (!style.empty())
			b["style"] = Suno::WideToUtf8(style);
		if (!title.empty())
			b["title"] = Suno::WideToUtf8(title);
	}
	else if (mode == SUNO_MODE_GENERATE_SOUNDS)
	{
		b["soundLoop"] = false;
		b["soundTempo"] = 120;
		b["soundKey"] = "Any";
		b["grabLyrics"] = false;
	}
	else if (mode == SUNO_MODE_COVER_AUDIO || mode == SUNO_MODE_ADD_VOCALS)
	{
		b["uploadUrl"] = Suno::WideToUtf8(source);
		if (mode == SUNO_MODE_COVER_AUDIO)
		{
			b["customMode"] = s->customEnabled;
			b["instrumental"] = s->instrumentalEnabled;
		}
		else
			b["negativeTags"] = Suno::WideToUtf8(negativeTags);
		if (!style.empty())
			b["style"] = Suno::WideToUtf8(style);
		if (!title.empty())
			b["title"] = Suno::WideToUtf8(title);
	}
	if (mode == SUNO_MODE_GENERATE_LYRICS)
		b.erase("model");
	Status(s, Tr(L"Sending request..."));
	auto r = Suno::Request(key, L"POST", Endpoint(mode), b.dump());
	if (!r.error.empty())
	{
		Status(s, Tr(L"Network error: ") + r.error);
		done();
		return;
	}
	json response;
	try
	{
		response = json::parse(r.body);
	}
	catch (...)
	{
		Status(s, Tr(L"Invalid API response (HTTP ") + std::to_wstring(r.status) + L")");
		done();
		return;
	}
	if (r.status < SUNO_HTTP_SUCCESS_MIN || r.status >= SUNO_HTTP_SUCCESS_MAX || response.value("code", 0) != SUNO_API_SUCCESS_CODE)
	{
		Status(s, Tr(L"API error: ") + Suno::Utf8ToWide(response.value("msg", r.body)));
		done();
		return;
	}
	std::string task = response["data"].value("taskId", "");
	if (task.empty())
	{
		Status(s, Tr(L"API did not return taskId."));
		done();
		return;
	}
	for (int attempt = 0; attempt < SUNO_TASK_POLL_ATTEMPTS; attempt++)
	{
		Status(s, Tr(L"Processing task ") + Suno::Utf8ToWide(task) + L"...");
		Sleep(SUNO_TASK_POLL_INTERVAL_MS);
		auto q = Suno::Request(key, L"GET", RecordEndpoint(mode, task));
		if (!q.error.empty())
			continue;
		try
		{
			response = json::parse(q.body);
		}
		catch (...)
		{
			continue;
		}
		if (response.value("code", 0) != SUNO_API_SUCCESS_CODE)
			continue;
		json data = response["data"];
		std::string st = data.value("status", "");
		if (st == "FAILED" || st == "ERROR" || st == "CREATE_TASK_FAILED" || st == "GENERATE_AUDIO_FAILED" || st == "GENERATE_LYRICS_FAILED" || st == "SENSITIVE_WORD_ERROR" || st == "CALLBACK_EXCEPTION")
		{
			Status(s, Tr(L"Generation failed: ") + Suno::Utf8ToWide(data.value("errorMessage", st)));
			done();
			return;
		}
		if (st != "SUCCESS")
			continue;
		if (mode == SUNO_MODE_GENERATE_LYRICS)
		{
			const json lyrics = data.value("response", json::object()).value("data", json::array());
			auto *message = new GeneratedLyrics;
			if (lyrics.is_array())
			{
				for (const auto &variant : lyrics)
				{
					const std::string text = variant.value("text", "");
					if (text.empty())
						continue;
					message->variants.push_back({Suno::Utf8ToWide(text), Suno::Utf8ToWide(variant.value("title", ""))});
					if (message->variants.size() == 2)
						break;
				}
			}
			if (message->variants.empty())
			{
				delete message;
				Status(s, Tr(L"Lyrics generation completed, but no text was returned."));
				done();
				return;
			}
			if (!PostMessageW(s->p->window, WM_APP + 6, 0, reinterpret_cast<LPARAM>(message)))
			{
				delete message;
				Status(s, Tr(L"Unable to show generated lyrics."));
				done();
				return;
			}
			Status(s, Tr(L"Choose one of the generated lyrics variants."));
			done();
			return;
		}
		json tracks;
		if (data.contains("response"))
		{
			auto rr = data["response"];
			if (rr.contains("sunoData"))
				tracks = rr["sunoData"];
			else if (rr.contains("data"))
				tracks = rr["data"];
		}
		if (!tracks.is_array())
		{
			Status(s, Tr(L"Task completed, but no audio was returned."));
			done();
			return;
		}
		int index = 1, downloaded = 0, coversDownloaded = 0;
		bool callbackQueued = false;
		const int trackCount = static_cast<int>(tracks.size());
		std::wstring fileName = SanitizeFileName(title);
		if (fileName.empty())
			fileName = L"Suno_" + Suno::Utf8ToWide(task);
		for (auto &t : tracks)
		{
			const int itemIndex = index++;
			const std::wstring basePath = s->p->workDirectory + L"\\" + fileName + L"_" + std::to_wstring(itemIndex);

			auto downloadAsset = [&](const std::vector<const char *> &fields, const std::wstring &target, std::wstring &error, std::string *downloadedUrl = nullptr) {
				for (const char *field : fields)
				{
					const std::string url = t.value(field, "");
					if (url.empty())
						continue;
					for (int attempt = 0; attempt < SUNO_DOWNLOAD_ATTEMPTS; ++attempt)
					{
						if (Suno::Download(Suno::Utf8ToWide(url), target, error))
						{
							if (downloadedUrl)
								*downloadedUrl = url;
							return true;
						}
						if (attempt + 1 < SUNO_DOWNLOAD_ATTEMPTS)
							Sleep(SUNO_DOWNLOAD_RETRY_INTERVAL_MS);
					}
				}
				return false;
			};

			std::wstring audioError;
			std::string audioUrl;
			const std::wstring audioFile = basePath + L".mp3";
			if (downloadAsset({"audioUrl", "sourceAudioUrl", "streamAudioUrl", "sourceStreamAudioUrl"}, audioFile, audioError, &audioUrl))
			{
				++downloaded;
				const std::string audioId = t.value("id", t.value("audioId", t.value("audio_id", "")));
				SaveTrackToCache(s, audioFile, task, audioId, audioUrl);
				callbackQueued = NotifyHost(s, audioFile) || callbackQueued;
			}

			std::wstring imageError;
			const std::wstring imageFile = basePath + L".jpeg";
			if (downloadAsset({"imageUrl", "sourceImageUrl", "image_url", "source_image_url"}, imageFile, imageError))
				++coversDownloaded;
		}
		Status(s, L"Completed. Downloaded " + std::to_wstring(downloaded) + L"/" + std::to_wstring(trackCount) + L" audio files and " + std::to_wstring(coversDownloaded) + L"/" + std::to_wstring(trackCount) + L" cover images.");
		done();
		if (callbackQueued)
			PostMessageW(s->p->window, WM_APP + 7, 0, 0);
		return;
	}
	Status(s, Tr(L"Timed out. The task can still finish on Suno API."));
	done();
}

LRESULT CALLBACK Proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	State *s = (State *)GetWindowLongPtrW(w, GWLP_USERDATA);
	if (m == WM_NCCREATE)
	{
		s = new State;
		s->p = (CSunoApiPlugin *)((CREATESTRUCTW *)lp)->lpCreateParams;
		SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)s);
		s->p->window = w;
	}
	if (!s)
		return DefWindowProcW(w, m, wp, lp);

	if (m == WM_CREATE)
	{
		const std::vector<std::wstring> modes = {Tr(L"Generate music"), Tr(L"Extend music"),
								Tr(L"Generate lyrics"), Tr(L"Generate sounds"),
								Tr(L"Cover audio"), Tr(L"Add vocals")};

		const std::vector<std::wstring> models = {L"V5", L"V5_5", L"V4_5ALL", L"V4_5PLUS", L"V4_5", L"V4"};
		const std::vector<std::wstring> vocalGenders = {Tr(L"Male"), Tr(L"Female")};

		Label(w, Tr(L"Mode").c_str(), 16, 16);
		s->mode = AVS::CreateComboBox(w, (HMENU)ID_MODE, g_hInst, 130, 12, 275, 25, AVS::ComboBoxSettings::Create(), modes);
		
		Label(w, Tr(L"Model").c_str(), 465, 16, 50);
		s->model = AVS::CreateComboBox(w, (HMENU)ID_MODEL, g_hInst, 530, 12, 120, 25, AVS::ComboBoxSettings::Create(), models);

		s->custom = AVS::CreateButton(w, (HMENU)ID_CUSTOM, g_hInst, Tr(L"Custom mode").c_str(), 130, 50, 130, 25, AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable));
		s->instrumental = AVS::CreateButton(w, (HMENU)ID_INSTRUMENTAL, g_hInst, Tr(L"Instrumental").c_str(), 275, 50, 130, 25, AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable));
		s->help = AVS::CreateButton(w, (HMENU)ID_HELP, g_hInst, Tr(L"Help").c_str(), 560, 50, 90, 25, AVS::ButtonSettings::Create(AVS::Buttons::Default));

		s->titleCounter = AVS::CreateLabel(w, g_hInst, L"0/0", 550, 81, 100, 18, AVS::LabelSettings::Create(AVS::LabelType::Disabled, DT_RIGHT));
		s->titleLabel = Label(w, Tr(L"Title").c_str(), 16, 106);
		s->title = Edit(w, ID_TITLE, 130, 102, 520, 25);

		s->promptCounter = AVS::CreateLabel(w, g_hInst, L"0/0", 550, 133, 100, 18, AVS::LabelSettings::Create(AVS::LabelType::Disabled, DT_RIGHT));
		s->promptLabel = Label(w, Tr(L"Prompt").c_str(), 16, 159);
		s->prompt = Edit(w, ID_PROMPT, 130, 155, 520, 120, true);

		s->styleCounter = AVS::CreateLabel(w, g_hInst, L"0/0", 550, 281, 100, 18, AVS::LabelSettings::Create(AVS::LabelType::Disabled, DT_RIGHT));
		s->styleLabel = Label(w, Tr(L"Style").c_str(), 16, 309);
		s->style = Edit(w, ID_STYLE, 130, 305, 520, 25);
		s->vocalGenderLabel = Label(w, Tr(L"Voice").c_str(), 16, 347);
		s->vocalGender = AVS::CreateComboBox(w, (HMENU)ID_VOCAL_GENDER, g_hInst, 130, 343, 120, 25, AVS::ComboBoxSettings::Create(), vocalGenders);
		s->negativeTagsLabel = AVS::CreateLabel(w, g_hInst, Tr(L"Negative Tags").c_str(), 16, 347, 110, 20, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
		s->negativeTags = Edit(w, ID_NEGATIVE_TAGS, 130, 343, 520, 25);
		s->sourceLabel = AVS::CreateLabel(w, g_hInst, Tr(L"Audio ID").c_str(), 16, 347, 110, 20, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
		s->source = Edit(w, ID_SOURCE, 130, 343, 520, 25);
		s->file = AVS::CreateButton(w, (HMENU)ID_FILE, g_hInst, Tr(L"File").c_str(), 16, 343, 100, 25, AVS::ButtonSettings::Create(AVS::Buttons::Default));
		UpdateSourceField(s);
		UpdateCounters(s);
		s->settings = AVS::CreateButton(w, (HMENU)ID_SETTINGS, g_hInst, Tr(L"Settings").c_str(), 465, 428, 85, 30, AVS::ButtonSettings::Create(AVS::Buttons::Default));
		s->run = AVS::CreateButton(w, (HMENU)ID_RUN, g_hInst, Tr(L"Generate").c_str(), 560, 428, 90, 30, AVS::ButtonSettings::Create(AVS::Buttons::Primary));
		s->status = AVS::CreateLabel(w, g_hInst, Tr(L"Ready (Credits left: loading...)").c_str(), 16, 432, 440, 30, AVS::LabelSettings::Create(AVS::LabelType::Enabled));
		std::thread(LoadCredits, w, s->p).detach();
		
		return 0;
	}
	if (m == WM_COMMAND && HIWORD(wp) == EN_CHANGE && ((HWND)lp == s->title || (HWND)lp == s->prompt || (HWND)lp == s->style))
	{
		UpdateCounters(s);
		return 0;
	}
	if (m == WM_COMMAND && LOWORD(wp) == ID_MODE && HIWORD(wp) == AVS::COMBOBOX_SELECTED_INDEX_CHANDED)
	{
		UpdateSourceField(s);
		UpdateCounters(s);
		return 0;
	}
	if (m == WM_COMMAND && LOWORD(wp) == ID_MODEL && HIWORD(wp) == AVS::COMBOBOX_SELECTED_INDEX_CHANDED)
	{
		UpdateCounters(s);
		return 0;
	}
	if (m == WM_COMMAND && LOWORD(wp) == ID_CUSTOM)
	{
		s->customEnabled = !s->customEnabled;
		AVS::Button_SetSettings(s->custom, AVS::ButtonSettings::Create(s->customEnabled ? AVS::Buttons::ToggleGroupEnable : AVS::Buttons::ToggleGroupDisable), Tr(L"Custom mode"));
		UpdateSourceField(s);
		UpdateCounters(s);
		return 0;
	}
	if (m == WM_COMMAND && LOWORD(wp) == ID_INSTRUMENTAL)
	{
		s->instrumentalEnabled = !s->instrumentalEnabled;
		AVS::Button_SetSettings(s->instrumental, AVS::ButtonSettings::Create(s->instrumentalEnabled ? AVS::Buttons::ToggleGroupEnable : AVS::Buttons::ToggleGroupDisable), Tr(L"Instrumental"));
		return 0;
	}
	if (m == WM_COMMAND && LOWORD(wp) == ID_FILE)
	{
		wchar_t path[MAX_PATH] = {};
		OPENFILENAMEW dialog{};
		dialog.lStructSize = sizeof(dialog);
		dialog.hwndOwner = w;
		dialog.lpstrFile = path;
		dialog.nMaxFile = MAX_PATH;
		dialog.lpstrFilter = L"Audio files "
							 L"(*.mp3;*.wav;*.m4a;*.aac;*.flac)\0*.mp3;*.wav;*.m4a;"
							 L"*.aac;*.flac\0All files (*.*)\0*.*\0";
		dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
		if (!GetOpenFileNameW(&dialog))
			return 0;

		try
		{
			std::wstring audioId;
			std::wstring audioUrl;
			const std::string hash = CalcFileSHA256(path);
			if (!FindTrackInCache(s, hash, audioId, audioUrl))
			{
				AVS::Label_SetText(s->status, Tr(L"Could not find this file in cache.json."));
				return 0;
			}

			const int mode = AVS::ComboBox_GetCurrent(s->mode);
			if (mode == SUNO_MODE_EXTEND_MUSIC)
			{
				if (audioId.empty())
				{
					AVS::Label_SetText(s->status, Tr(L"Audio ID is missing for this file in cache.json."));
					return 0;
				}
				Set(s->source, audioId);
			}
			else if (mode == SUNO_MODE_COVER_AUDIO || mode == SUNO_MODE_ADD_VOCALS)
			{
				if (audioUrl.empty())
				{
					AVS::Label_SetText(s->status, Tr(L"Audio URL is missing for this file in cache.json."));
					return 0;
				}
				Set(s->source, audioUrl);
			}
			AVS::Label_SetText(s->status, mode == SUNO_MODE_COVER_AUDIO || mode == SUNO_MODE_ADD_VOCALS ? Tr(L"Audio URL loaded from cache.json.") : Tr(L"Audio ID loaded from cache.json."));
		}
		catch (const std::exception &error)
		{
			AVS::Label_SetText(s->status, Suno::Utf8ToWide(error.what()));
		}
		catch (...)
		{
			AVS::Label_SetText(s->status, Tr(L"Unable to calculate SHA-256 or read cache.json."));
		}
		return 0;
	}
	if (m == WM_COMMAND && LOWORD(wp) == ID_SETTINGS)
	{
		ShowSettings(w, s->p);
		return 0;
	}
	if (m == WM_COMMAND && LOWORD(wp) == ID_HELP)
	{
		HINSTANCE result = ShellExecuteW(w, L"open", SUNO_HELP_URL, nullptr, nullptr, SW_SHOWNORMAL);
		if (reinterpret_cast<INT_PTR>(result) <= 32)
			AVS::Label_SetText(s->status, Tr(L"Unable to open the Suno API documentation."));
		return 0;
	}
	if (m == WM_COMMAND && LOWORD(wp) == ID_RUN && !s->busy.exchange(true))
	{
		EnableWindow(s->run, FALSE);
		AVS::Label_SetText(s->status, Tr(L"Starting..."));
		std::thread(Worker, s).detach();
		return 0;
	}
	if (m == WM_APP + 1)
	{
		std::wstring *t = (std::wstring *)lp;
		AVS::Label_SetText(s->status, *t);
		delete t;
		return 0;
	}
	if (m == WM_APP + 2)
	{
		s->busy = false;
		EnableWindow(s->run, TRUE);
		return 0;
	}
	if (m == WM_APP + 3)
	{
		auto *text = reinterpret_cast<std::wstring *>(lp);
		if (!s->busy)
			AVS::Label_SetText(s->status, *text);
		delete text;
		return 0;
	}
	if (m == WM_APP + 4)
	{
		if (!s->busy)
		{
			AVS::Label_SetText(s->status, Tr(L"Ready (Credits left: loading...)"));
			std::thread(LoadCredits, w, s->p).detach();
		}
		return 0;
	}
	if (m == WM_APP + 5)
	{
		auto *path = reinterpret_cast<std::wstring *>(lp);
		if (s->p->callback && ReleaseActivationContext(s->p))
			s->p->callback(export_str(L"SunoApi.plugin"), export_str(path->c_str()), 0, s->p->callbackContext);
		delete path;
		return 0;
	}
	if (m == WM_APP + 6)
	{
		auto *lyrics = reinterpret_cast<GeneratedLyrics *>(lp);
		ShowLyricsSelection(w, s, lyrics);
		return 0;
	}
	if (m == WM_APP + 7)
	{
		CSunoApiPlugin *plugin = s->p;
		DestroyWindow(w);
		ReleaseActivationContext(plugin);
		return 0;
	}
	if (m == WM_SYSCOMMAND && (wp & 0xFFF0) == SC_CLOSE)
	{
		PostMessageW(w, WM_CLOSE, 0, 0);
		return 0;
	}
	if (m == WM_CLOSE)
	{
		if (s->busy)
		{
			MessageBoxW(w, Tr(L"Wait for the current task to finish.").c_str(), L"Suno API", MB_OK | MB_ICONINFORMATION);
			return 0;
		}
		CSunoApiPlugin *plugin = s->p;
		DestroyWindow(w);
		ReleaseActivationContext(plugin);
		return 0;
	}
	if (m == WM_NCDESTROY)
	{
		s->p->window = nullptr;
		delete s;
		SetWindowLongPtrW(w, GWLP_USERDATA, 0);
	}
	return DefWindowProcW(w, m, wp, lp);
}
} // namespace

void SunoUI::Show(CSunoApiPlugin *p)
{
	if (p->window && IsWindow(p->window))
	{
		SetForegroundWindow(p->window);
		return;
	}

	ACTCTXW activationContext{sizeof(activationContext)};
	activationContext.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
	activationContext.lpResourceName = MAKEINTRESOURCEW(1);
	activationContext.hModule = g_hInst;
	HANDLE context = CreateActCtxW(&activationContext);
	if (context != INVALID_HANDLE_VALUE)
	{
		ULONG_PTR cookie = 0;
		if (ActivateActCtx(context, &cookie))
		{
			p->activationContext = context;
			p->activationCookie = cookie;
			p->activationThreadId = GetCurrentThreadId();
		}
		else
			ReleaseActCtx(context);
	}

	INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_STANDARD_CLASSES};
	InitCommonControlsEx(&commonControls);
	const wchar_t *cls = L"SunoApiMainWindowClass";
	WNDCLASSEXW wc = {sizeof(wc)};
	if (!GetClassInfoExW(g_hInst, cls, &wc))
	{
		wc.lpfnWndProc = Proc;
		wc.hInstance = g_hInst;
		wc.lpszClassName = cls;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		SetWindowClassIcons(wc, p);
		AVS::Color background = AVS::Color::GetDefaultWindowBackground();
		wc.hbrBackground = CreateSolidBrush(RGB(background.R, background.G, background.B));
		RegisterClassExW(&wc);
	}
	
	HWND w = CreateWindowExW(0, cls, (L"Suno API " + Tr(L"Music Generator")).c_str(),
							WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
							690, 514, p->parentWindow,
							nullptr, g_hInst, p);
	if (!w)
	{
		ReleaseActivationContext(p);
		return;
	}
	
	CenterWindow(w);
	ShowWindow(w, SW_SHOW);
	UpdateWindow(w);
}

#include "suno_ui.h"
#include "../../../sdk/3dparty/nlohmann/json/single_include/nlohmann/json.hpp"
#include "../../../sdk/ui/winapi/ui.h"
#include "export_utils.h"
#include "sha256.h"
#include "suno_api.h"
#include <atomic>
#include <commctrl.h>
#include <fstream>
#include <thread>
#pragma comment(lib, "comctl32.lib")

using json = nlohmann::json;

#define SUNO_MODE_GENERATE_MUSIC 0
#define SUNO_MODE_EXTEND_MUSIC 1
#define SUNO_MODE_GENERATE_LYRICS 2
#define SUNO_MODE_GENERATE_SOUNDS 3
#define SUNO_MODE_COVER_AUDIO 4
#define SUNO_MODE_ADD_VOCALS 5
#define SUNO_MODE_VOCAL_REMOVAL 6

#define SUNO_API_SUCCESS_CODE 200
#define SUNO_HTTP_SUCCESS_MIN 200
#define SUNO_HTTP_SUCCESS_MAX 300
#define SUNO_TASK_POLL_ATTEMPTS 90
#define SUNO_TASK_POLL_INTERVAL_MS 4000
#define SUNO_DOWNLOAD_ATTEMPTS 3
#define SUNO_DOWNLOAD_RETRY_INTERVAL_MS 750

namespace
{
enum
{
	ID_MODE = 100,
	ID_MODEL,
	ID_PROMPT,
	ID_STYLE,
	ID_TITLE,
	ID_SOURCE,
	ID_FILE,
	ID_CUSTOM,
	ID_INSTRUMENTAL,
	ID_RUN,
	ID_SETTINGS,
	ID_STATUS
};

struct State
{
	CSunoApiPlugin *p = nullptr;
	HWND mode{}, model{}, prompt{}, style{}, title{}, sourceLabel{}, source{}, audioIdLabel{}, audioId{}, titleCounter{}, promptCounter{}, styleCounter{}, custom{}, instrumental{}, file{}, settings{}, run{}, status{};
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

void Label(HWND w, const wchar_t *s, int x, int y, int width = 110)
{
	AVS::CreateLabel(w, g_hInst, s, x, y, width, 20, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
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

void NotifyHost(State *state, const std::wstring &path)
{
	if (!state->p->callback)
		return;
	
	auto *message = new std::wstring(path);
	
	if (!PostMessageW(state->p->window, WM_APP + 5, 0, reinterpret_cast<LPARAM>(message)))
		delete message;
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
	case SUNO_MODE_VOCAL_REMOVAL:
		return L"/api/v1/vocal-removal/generate";
	default:
		return L"/api/v1/generate";
	}
}

std::wstring RecordEndpoint(int mode, const std::string &id)
{
	if (mode == SUNO_MODE_GENERATE_LYRICS)
		return L"/api/v1/lyrics/record-info?taskId=" + Suno::Utf8ToWide(id);
	if (mode == SUNO_MODE_VOCAL_REMOVAL)
		return L"/api/v1/vocal-removal/record-info?taskId=" + Suno::Utf8ToWide(id);
	return L"/api/v1/generate/record-info?taskId=" + Suno::Utf8ToWide(id);
}

std::wstring CachePath(const State *state)
{
	return state->p->workDirectory + L"\\cache.json";
}

bool FindIdInCache(State *state, const std::string &hash, std::wstring &audioId)
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
			return !audioId.empty();
		}
		if (entry.is_object())
		{
			audioId = Suno::Utf8ToWide(entry.value("audioId", ""));
			return !audioId.empty();
		}
		return false;
	}
	catch (...)
	{
		return false;
	}
}

void SaveIdToCache(State *state, const std::wstring &filePath, const std::string &audioId)
{
	if (audioId.empty())
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
		cache[hash] = audioId;
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (output.is_open())
			output << cache.dump(4);
	}
	catch (...)
	{
	}
}

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
	const std::wstring text = std::to_wstring(length) + L"/" + (limit > 0 ? std::to_wstring(limit) : L"N/A");
	AVS::Label_SetText(counter, text);
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
	bool visible = mode == SUNO_MODE_EXTEND_MUSIC || mode == SUNO_MODE_COVER_AUDIO || mode == SUNO_MODE_ADD_VOCALS || mode == SUNO_MODE_VOCAL_REMOVAL;
	if (mode == SUNO_MODE_COVER_AUDIO || mode == SUNO_MODE_ADD_VOCALS)
		AVS::Label_SetText(state->sourceLabel, L"URL");
	else if (mode == SUNO_MODE_VOCAL_REMOVAL)
		AVS::Label_SetText(state->sourceLabel, L"Task ID");

	const bool fileMode = mode == SUNO_MODE_EXTEND_MUSIC || mode == SUNO_MODE_VOCAL_REMOVAL;
	ShowWindow(state->sourceLabel, visible && mode != SUNO_MODE_EXTEND_MUSIC ? SW_SHOW : SW_HIDE);
	ShowWindow(state->source, visible ? SW_SHOW : SW_HIDE);

	const bool showAudioId = mode == SUNO_MODE_VOCAL_REMOVAL;
	ShowWindow(state->audioIdLabel, SW_HIDE);
	ShowWindow(state->audioId, showAudioId ? SW_SHOW : SW_HIDE);
	ShowWindow(state->file, fileMode ? SW_SHOW : SW_HIDE);
	SetWindowPos(state->file, nullptr, 16, mode == SUNO_MODE_VOCAL_REMOVAL ? 381 : 343, 100, 25, SWP_NOZORDER | SWP_NOACTIVATE);
	if (!visible)
		Set(state->source, L"");
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

		AVS::CreateButton(hwnd, (HMENU)0x8201, g_hInst, L"Save", 265, 55, 80, 25, AVS::ButtonSettings::Create(AVS::Buttons::Primary));
		AVS::CreateButton(hwnd, (HMENU)0x8202, g_hInst, L"Cancel", 175, 55, 80, 25, AVS::ButtonSettings::Create(AVS::Buttons::Default));
		AVS::CreateButton(hwnd, (HMENU)0x8203, g_hInst, L"Delete API key", 15, 55, 130, 25, AVS::ButtonSettings::Create(AVS::Buttons::Default));
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
		AVS::Color background = AVS::Color::GetDefaultWindowBackground();
		windowClass.hbrBackground = CreateSolidBrush(RGB(background.R, background.G, background.B));
		RegisterClassExW(&windowClass);
	}

	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
	RECT rect{0, 0, 360, 95};
	AdjustWindowRectEx(&rect, style, FALSE, 0);
	
	HWND window = CreateWindowExW(0, className,
									L"Suno API Settings",
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
		status = L"Ready (Credits left: API key required)";
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
				status = L"Ready (Credits left: " + Suno::Utf8ToWide(credits) + L")";
			}
			else
			{
				status = L"Ready (Credits left: unavailable)";
			}
		}
		catch (...)
		{
			status = L"Ready (Credits left: unavailable)";
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
		Status(s, L"Open API key settings first.");
		done();
		return;
	}
	int mode = AVS::ComboBox_GetCurrent(s->mode);
	std::wstring prompt = Text(s->prompt), style = Text(s->style), title = Text(s->title), source = Text(s->source), model = AVS::ComboBox_GetCurrentText(s->model), audioId = Text(s->audioId);
	json b;
	b["prompt"] = Suno::WideToUtf8(prompt);
	b["model"] = Suno::WideToUtf8(model);
	if (mode == SUNO_MODE_GENERATE_MUSIC)
	{
		b["customMode"] = s->customEnabled;
		b["instrumental"] = s->instrumentalEnabled;
		if (!style.empty())
			b["style"] = Suno::WideToUtf8(style);
		if (!title.empty())
			b["title"] = Suno::WideToUtf8(title);
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
		if (!style.empty())
			b["style"] = Suno::WideToUtf8(style);
		if (!title.empty())
			b["title"] = Suno::WideToUtf8(title);
	}
	else if (mode == SUNO_MODE_VOCAL_REMOVAL)
	{
		b.clear();
		b["taskId"] = Suno::WideToUtf8(source);
		b["audioId"] = Suno::WideToUtf8(audioId);
			b["type"] = "separate_vocal";
	}
	if (mode == SUNO_MODE_GENERATE_LYRICS)
		b.erase("model");
	Status(s, L"Sending request...");
	auto r = Suno::Request(key, L"POST", Endpoint(mode), b.dump());
	if (!r.error.empty())
	{
		Status(s, L"Network error: " + r.error);
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
		Status(s, L"Invalid API response (HTTP " + std::to_wstring(r.status) + L")");
		done();
		return;
	}
	if (r.status < SUNO_HTTP_SUCCESS_MIN || r.status >= SUNO_HTTP_SUCCESS_MAX || response.value("code", 0) != SUNO_API_SUCCESS_CODE)
	{
		Status(s, L"API error: " + Suno::Utf8ToWide(response.value("msg", r.body)));
		done();
		return;
	}
	std::string task = response["data"].value("taskId", "");
	if (task.empty())
	{
		Status(s, L"API did not return taskId.");
		done();
		return;
	}
	for (int attempt = 0; attempt < SUNO_TASK_POLL_ATTEMPTS; attempt++)
	{
		Status(s, L"Processing task " + Suno::Utf8ToWide(task) + L"...");
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
		std::string st = mode == SUNO_MODE_VOCAL_REMOVAL ? data.value("successFlag", "") : data.value("status", "");
		if (st == "FAILED" || st == "ERROR" || st == "CREATE_TASK_FAILED" || st == "GENERATE_AUDIO_FAILED" || st == "CALLBACK_EXCEPTION")
		{
			Status(s, L"Generation failed: " + Suno::Utf8ToWide(data.value("errorMessage", st)));
			done();
			return;
		}
		if (st != "SUCCESS")
			continue;
		if (mode == SUNO_MODE_GENERATE_LYRICS)
		{
			std::wstring file = s->p->workDirectory + L"\\SunoLyrics_" + Suno::Utf8ToWide(task) + L".txt";
			std::ofstream f(file, std::ios::binary);
			std::string text = data.dump(2);
			f.write(text.data(), text.size());
			f.close();
			NotifyHost(s, file);
			Status(s, L"Lyrics saved: " + file);
			done();
			return;
		}
		if (mode == SUNO_MODE_VOCAL_REMOVAL)
		{
			json result = data.value("response", json::object());
			const std::vector<std::pair<std::string, std::wstring>> stems = {{"vocalUrl", L"Vocals"}, {"instrumentalUrl", L"Instrumental"}};
			int downloaded = 0;
			for (const auto &stem : stems)
			{
				std::string url = result.value(stem.first, "");
				if (url.empty())
					continue;
				std::wstring file = s->p->workDirectory + L"\\Suno_" + Suno::Utf8ToWide(task) + L"_" + stem.second + L".mp3";
				std::wstring error;
				if (Suno::Download(Suno::Utf8ToWide(url), file, error))
				{
					++downloaded;
					NotifyHost(s, file);
				}
			}
			Status(s, downloaded == 2 ? L"Vocal and instrumental stems were added to the application."
									  : L"Separation completed, but some stem files could not be "
										L"downloaded.");
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
			Status(s, L"Task completed, but no audio was returned.");
			done();
			return;
		}
		int index = 1, downloaded = 0, coversDownloaded = 0;
		const int trackCount = static_cast<int>(tracks.size());
		std::wstring fileName = SanitizeFileName(title);
		if (fileName.empty())
			fileName = L"Suno_" + Suno::Utf8ToWide(task);
		for (auto &t : tracks)
		{
			const int itemIndex = index++;
			const std::wstring basePath = s->p->workDirectory + L"\\" + fileName + L"_" + std::to_wstring(itemIndex);

			auto downloadAsset = [&](const std::vector<const char *> &fields, const std::wstring &target, std::wstring &error) {
				for (const char *field : fields)
				{
					const std::string url = t.value(field, "");
					if (url.empty())
						continue;
					for (int attempt = 0; attempt < SUNO_DOWNLOAD_ATTEMPTS; ++attempt)
					{
						if (Suno::Download(Suno::Utf8ToWide(url), target, error))
							return true;
						if (attempt + 1 < SUNO_DOWNLOAD_ATTEMPTS)
							Sleep(SUNO_DOWNLOAD_RETRY_INTERVAL_MS);
					}
				}
				return false;
			};

			std::wstring audioError;
			const std::wstring audioFile = basePath + L".mp3";
			if (downloadAsset({"audioUrl", "sourceAudioUrl", "streamAudioUrl", "sourceStreamAudioUrl"}, audioFile, audioError))
			{
				++downloaded;
				const std::string audioId = t.value("id", t.value("audioId", t.value("audio_id", "")));
				SaveIdToCache(s, audioFile, audioId);
				NotifyHost(s, audioFile);
			}

			std::wstring imageError;
			const std::wstring imageFile = basePath + L".jpeg";
			if (downloadAsset({"imageUrl", "sourceImageUrl", "image_url", "source_image_url"}, imageFile, imageError))
				++coversDownloaded;
		}
		Status(s, L"Completed. Downloaded " + std::to_wstring(downloaded) + L"/" + std::to_wstring(trackCount) + L" audio files and " + std::to_wstring(coversDownloaded) + L"/" + std::to_wstring(trackCount) + L" cover images.");
		done();
		return;
	}
	Status(s, L"Timed out. The task can still finish on Suno API.");
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
		const std::vector<std::wstring> modes = {L"Generate music", L"Extend music",
												L"Generate lyrics", L"Generate sounds",
												L"Cover audio", L"Add vocals", L"Vocal Removal"};

		const std::vector<std::wstring> models = {L"V5", L"V5_5", L"V4_5ALL", L"V4_5PLUS", L"V4_5", L"V4"};

		Label(w, L"Mode", 16, 16);
		s->mode = AVS::CreateComboBox(w, (HMENU)ID_MODE, g_hInst, 130, 12, 260, 25, AVS::ComboBoxSettings::Create(), modes);
		
		Label(w, L"Model", 410, 16, 60);
		s->model = AVS::CreateComboBox(w, (HMENU)ID_MODEL, g_hInst, 470, 12, 180, 25, AVS::ComboBoxSettings::Create(), models);

		s->custom = AVS::CreateButton(w, (HMENU)ID_CUSTOM, g_hInst, L"Custom mode", 130, 50, 130, 25, AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable));
		s->instrumental = AVS::CreateButton(w, (HMENU)ID_INSTRUMENTAL, g_hInst, L"Instrumental", 275, 50, 130, 25, AVS::ButtonSettings::Create(AVS::Buttons::ToggleGroupDisable));

		s->titleCounter = AVS::CreateLabel(w, g_hInst, L"0/0", 550, 81, 100, 18, AVS::LabelSettings::Create(AVS::LabelType::Disabled, DT_RIGHT));
		Label(w, L"Title", 16, 106);
		s->title = Edit(w, ID_TITLE, 130, 102, 520, 25);

		s->promptCounter = AVS::CreateLabel(w, g_hInst, L"0/0", 550, 133, 100, 18, AVS::LabelSettings::Create(AVS::LabelType::Disabled, DT_RIGHT));
		Label(w, L"Prompt / lyrics", 16, 159);
		s->prompt = Edit(w, ID_PROMPT, 130, 155, 520, 120, true);

		s->styleCounter = AVS::CreateLabel(w, g_hInst, L"0/0", 550, 281, 100, 18, AVS::LabelSettings::Create(AVS::LabelType::Disabled, DT_RIGHT));
		Label(w, L"Style", 16, 309);
		s->style = Edit(w, ID_STYLE, 130, 305, 520, 25);
		s->sourceLabel = AVS::CreateLabel(w, g_hInst, L"Audio ID", 16, 347, 110, 20, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
		s->source = Edit(w, ID_SOURCE, 130, 343, 520, 25);
		s->audioIdLabel = AVS::CreateLabel(w, g_hInst, L"Audio ID", 16, 385, 110, 20, AVS::LabelSettings::Create(AVS::LabelType::Disabled));
		s->audioId = Edit(w, 0, 130, 381, 520, 25);
		s->file = AVS::CreateButton(w, (HMENU)ID_FILE, g_hInst, L"File", 16, 343, 100, 25, AVS::ButtonSettings::Create(AVS::Buttons::Default));
		UpdateSourceField(s);
		UpdateCounters(s);
		s->settings = AVS::CreateButton(w, (HMENU)ID_SETTINGS, g_hInst, L"Settings", 465, 428, 85, 30, AVS::ButtonSettings::Create(AVS::Buttons::Default));
		s->run = AVS::CreateButton(w, (HMENU)ID_RUN, g_hInst, L"Generate", 560, 428, 90, 30, AVS::ButtonSettings::Create(AVS::Buttons::Primary));
		s->status = AVS::CreateLabel(w, g_hInst, L"Ready (Credits left: loading...)", 16, 432, 430, 30, AVS::LabelSettings::Create(AVS::LabelType::Enabled));
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
		AVS::Button_SetSettings(s->custom, AVS::ButtonSettings::Create(s->customEnabled ? AVS::Buttons::ToggleGroupEnable : AVS::Buttons::ToggleGroupDisable), L"Custom mode");
		UpdateCounters(s);
		return 0;
	}
	if (m == WM_COMMAND && LOWORD(wp) == ID_INSTRUMENTAL)
	{
		s->instrumentalEnabled = !s->instrumentalEnabled;
		AVS::Button_SetSettings(s->instrumental, AVS::ButtonSettings::Create(s->instrumentalEnabled ? AVS::Buttons::ToggleGroupEnable : AVS::Buttons::ToggleGroupDisable), L"Instrumental");
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
			const std::string hash = CalcFileSHA256(path);
			if (!FindIdInCache(s, hash, audioId))
			{
				AVS::Label_SetText(s->status, L"Could not find this file in cache.json.");
				return 0;
			}

			const int mode = AVS::ComboBox_GetCurrent(s->mode);
			if (mode == SUNO_MODE_EXTEND_MUSIC)
				Set(s->source, audioId);
			else if (mode == SUNO_MODE_VOCAL_REMOVAL)
				Set(s->audioId, audioId);
			AVS::Label_SetText(s->status, L"Audio ID loaded from cache.json.");
		}
		catch (const std::exception &error)
		{
			AVS::Label_SetText(s->status, Suno::Utf8ToWide(error.what()));
		}
		catch (...)
		{
			AVS::Label_SetText(s->status, L"Unable to calculate SHA-256 or read cache.json.");
		}
		return 0;
	}
	if (m == WM_COMMAND && LOWORD(wp) == ID_SETTINGS)
	{
		ShowSettings(w, s->p);
		return 0;
	}
	if (m == WM_COMMAND && LOWORD(wp) == ID_RUN && !s->busy.exchange(true))
	{
		EnableWindow(s->run, FALSE);
		AVS::Label_SetText(s->status, L"Starting...");
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
			AVS::Label_SetText(s->status, L"Ready (Credits left: loading...)");
			std::thread(LoadCredits, w, s->p).detach();
		}
		return 0;
	}
	if (m == WM_APP + 5)
	{
		auto *path = reinterpret_cast<std::wstring *>(lp);
		if (s->p->callback)
			s->p->callback(export_str(L"SunoApi.plugin"), export_str(path->c_str()), 0, s->p->callbackContext);
		delete path;
		return 0;
	}
	if (m == WM_CLOSE)
	{
		if (s->busy)
		{
			MessageBoxW(w, L"Wait for the current task to finish.", L"Suno API", MB_OK | MB_ICONINFORMATION);
			return 0;
		}
		DestroyWindow(w);
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
	const wchar_t *cls = L"SunoApiMainWindowClass";
	WNDCLASSEXW wc = {sizeof(wc)};
	if (!GetClassInfoExW(g_hInst, cls, &wc))
	{
		wc.lpfnWndProc = Proc;
		wc.hInstance = g_hInst;
		wc.lpszClassName = cls;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		AVS::Color background = AVS::Color::GetDefaultWindowBackground();
		wc.hbrBackground = CreateSolidBrush(RGB(background.R, background.G, background.B));
		RegisterClassExW(&wc);
	}
	
	HWND w = CreateWindowExW(0, cls, L"Suno API Music Generator",
							WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
							690, 514, p->parentWindow,
							nullptr, g_hInst, p);
	
	CenterWindow(w);
	ShowWindow(w, SW_SHOW);
	UpdateWindow(w);
}

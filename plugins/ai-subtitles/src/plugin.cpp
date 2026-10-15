#include "plugin.h"
#include "../../../sdk/3dParty/nlohmann/json/single_include/nlohmann/json.hpp"
#include "../../../sdk/common/utils.h"
#include "export_utils.h"
#include "resource.h"
#include "ui.h"
#include <fstream>
#include <shlobj.h>

namespace {
	std::filesystem::path ModulePath() {
		wchar_t path[32768]{};
		GetModuleFileNameW(g_module, path, (DWORD)std::size(path));
		return std::filesystem::path(path).parent_path();
	}

	// the icon is compiled in; the host and the window title bar want a file
	void ExtractIcon(const std::filesystem::path& workDirectory) {
		HRSRC resource = FindResourceW(g_module, MAKEINTRESOURCEW(IDR_ICON), RT_RCDATA);
		HGLOBAL loaded = resource ? LoadResource(g_module, resource) : nullptr;
		const void* data = loaded ? LockResource(loaded) : nullptr;
		const DWORD size = resource ? SizeofResource(g_module, resource) : 0;
		if (!data || !size)
			return;
		for (const wchar_t* name : { L"icon_internal.ico", L"icon.ico" }) {
			std::ofstream output(workDirectory / name, std::ios::binary | std::ios::trunc);
			output.write(static_cast<const char*>(data), size);
		}
	}
} // namespace

CSubtitlesPlugin::CSubtitlesPlugin() : moduleDirectory(ModulePath()) {
	wchar_t localAppData[MAX_PATH]{};
	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData)))
		workDirectory = std::filesystem::path(localAppData) / L"avs_plugin_aisubtitles";
	else
		workDirectory = std::filesystem::temp_directory_path() / L"avs_plugin_aisubtitles";
	std::error_code error;
	std::filesystem::create_directories(workDirectory / L"models", error);
	// scratch folders of a run that did not finish
	std::filesystem::remove_all(workDirectory / L"tmp", error);
	ExtractIcon(workDirectory);
	LoadSettings();
}

CSubtitlesPlugin::~CSubtitlesPlugin() {
	Cancel();
	JoinWorker();
}

void CSubtitlesPlugin::LoadSettings() {
	std::ifstream input(workDirectory / L"settings.json", std::ios::binary);
	if (!input)
		return;
	try {
		const auto json = nlohmann::json::parse(input);
		const std::wstring savedModel =
			NSStringUtils::utf8_to_wstring(json.value("model", std::string("small")));
		if (FindWhisperModel(savedModel))
			model = savedModel;
		language = NSStringUtils::utf8_to_wstring(json.value("language", std::string("auto")));
		translate = json.value("translate", false);
		speechOnly = json.value("content", std::string("speech")) != "music";
		useGpu = json.value("useGpu", true);
	}
	catch (...) {
		// a damaged settings file means defaults
	}
}

void CSubtitlesPlugin::SaveSettings() const {
	nlohmann::json json;
	json["model"] = NSStringUtils::wstring_to_utf8(model);
	json["language"] = NSStringUtils::wstring_to_utf8(language);
	json["translate"] = translate;
	json["content"] = speechOnly ? "speech" : "music";
	json["useGpu"] = useGpu;
	std::ofstream output(workDirectory / L"settings.json", std::ios::binary | std::ios::trunc);
	output << json.dump(2);
}

SubtitleTools CSubtitlesPlugin::Tools() const {
	SubtitleTools tools;
	tools.toolsDirectory = moduleDirectory;
	tools.workDirectory = workDirectory;
	tools.translate = [](const wchar_t* text) { return Translate(text); };
	return tools;
}

void CSubtitlesPlugin::JoinWorker() {
	std::lock_guard<std::mutex> lock(workerMutex);
	if (worker.joinable())
		worker.join();
}

void CSubtitlesPlugin::Cancel() { cancel = true; }

void CSubtitlesPlugin::PostStatus(const std::wstring& text, int progress, bool finished,
	bool success) {
	if (window && IsWindow(window))
		PostMessageW(window, WM_SUBTITLES_STATUS, 0,
			reinterpret_cast<LPARAM>(new UiStatus{ text, progress, finished, success }));
}

void CSubtitlesPlugin::Start(SubtitleJob job) {
	if (busy.exchange(true))
		return;
	JoinWorker();
	cancel = false;
	worker = std::thread([this, job = std::move(job)] {
		std::wstring error;
		const bool ok = MakeSubtitles(Tools(), job,
			[this](const std::wstring& text, int percent) { PostStatus(text, percent, false, false); },
			cancel, error);
		busy = false;
		PostStatus(ok ? Translate(L"Subtitles are ready") + L"\r\n" + job.output.wstring()
			: (cancel ? Translate(L"Cancelled") : error),
			ok ? 100 : -1, true, ok);
	});
}

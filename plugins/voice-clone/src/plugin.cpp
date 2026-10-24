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

CVoiceClonePlugin::CVoiceClonePlugin() : moduleDirectory(ModulePath()) {
	wchar_t localAppData[MAX_PATH]{};
	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData)))
		workDirectory = std::filesystem::path(localAppData) / L"avs_plugin_voiceclone";
	else
		workDirectory = std::filesystem::temp_directory_path() / L"avs_plugin_voiceclone";
	std::error_code error;
	std::filesystem::create_directories(workDirectory / L"models", error);
	// scratch folders of a run that did not finish
	std::filesystem::remove_all(workDirectory / L"tmp", error);
	ExtractIcon(workDirectory);
	LoadSettings();
}

CVoiceClonePlugin::~CVoiceClonePlugin() {
	Cancel();
	JoinWorker();
}

void CVoiceClonePlugin::LoadSettings() {
	std::ifstream input(workDirectory / L"settings.json", std::ios::binary);
	if (!input)
		return;
	try {
		const auto json = nlohmann::json::parse(input);
		voice = NSStringUtils::utf8_to_wstring(json.value("voice", std::string()));
		const std::wstring savedQuality = NSStringUtils::utf8_to_wstring(json.value("quality", std::string("standard")));
		if (FindVoiceQuality(savedQuality))
			quality = savedQuality;
		const std::wstring savedFormat = NSStringUtils::utf8_to_wstring(json.value("format", std::string("wav")));
		if (FindVoiceFormat(savedFormat))
			format = savedFormat;
		pitch = (std::max)(-12, (std::min)(12, json.value("pitch", 0)));
		matchVoicePitch = json.value("matchVoicePitch", false);
		useGpu = json.value("useGpu", true);
		addToProject = json.value("addToProject", true);
	}
	catch (...) {
		// a damaged settings file means defaults
	}
}

void CVoiceClonePlugin::SaveSettings() const {
	nlohmann::json json;
	json["voice"] = NSStringUtils::wstring_to_utf8(voice.wstring());
	json["quality"] = NSStringUtils::wstring_to_utf8(quality);
	json["format"] = NSStringUtils::wstring_to_utf8(format);
	json["pitch"] = pitch;
	json["matchVoicePitch"] = matchVoicePitch;
	json["useGpu"] = useGpu;
	json["addToProject"] = addToProject;
	std::ofstream output(workDirectory / L"settings.json", std::ios::binary | std::ios::trunc);
	output << json.dump(2);
}

VoiceTools CVoiceClonePlugin::Tools() const {
	VoiceTools tools;
	tools.toolsDirectory = moduleDirectory;
	tools.workDirectory = workDirectory;
	tools.translate = [](const wchar_t* text) { return Translate(text); };
	return tools;
}

void CVoiceClonePlugin::JoinWorker() {
	std::lock_guard<std::mutex> lock(workerMutex);
	if (worker.joinable())
		worker.join();
}

void CVoiceClonePlugin::Cancel() { cancel = true; }

void CVoiceClonePlugin::PostStatus(const std::wstring& text, int progress, bool finished, bool success) {
	if (window && IsWindow(window))
		PostMessageW(window, WM_VOICE_STATUS, 0,
			reinterpret_cast<LPARAM>(new UiStatus{ text, progress, finished, success }));
}

void CVoiceClonePlugin::Start(VoiceJob job) {
	if (busy.exchange(true))
		return;
	JoinWorker();
	cancel = false;
	worker = std::thread([this, job = std::move(job)] {
		std::wstring error;
		const VoiceFormat* chosen = FindVoiceFormat(format);
		const bool ok = ConvertVoice(Tools(), job, chosen ? *chosen : VoiceFormats().front(),
			[this](const std::wstring& text, int percent) { PostStatus(text, percent, false, false); },
			cancel, error);
		busy = false;
		PostStatus(ok ? Translate(L"The new voice is ready") + L"\r\n" + job.output.wstring()
			: (cancel ? Translate(L"Cancelled") : error),
			ok ? 100 : -1, true, ok);
	});
}

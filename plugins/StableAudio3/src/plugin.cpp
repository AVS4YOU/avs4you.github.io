#include "plugin.h"
#include "resource.h"
#include "sa3_backend.h"
#include "ui.h"
#include <chrono>
#include <fstream>
#include <shlobj.h>
static std::filesystem::path ModulePath() {
	wchar_t p[32768]{};
	GetModuleFileNameW(g_module, p, (DWORD)std::size(p));
	return std::filesystem::path(p).parent_path();
}
CStableAudio3Plugin::CStableAudio3Plugin() : moduleDirectory(ModulePath()) {
	wchar_t localAppData[MAX_PATH]{};
	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
		localAppData)))
		workDirectory =
		std::filesystem::path(localAppData) / L"avs_plugin_stableaudio3";
	else
		workDirectory =
		std::filesystem::temp_directory_path() / L"avs_plugin_stableaudio3";

	std::error_code error;
	std::filesystem::create_directories(workDirectory / L"models", error);

	const auto internalIcon = workDirectory / L"icon_internal.ico";
	const auto publicIcon = workDirectory / L"icon.ico";
	HRSRC iconResource =
		FindResourceW(g_module, MAKEINTRESOURCEW(IDR_ICON), RT_RCDATA);
	if (iconResource) {
		HGLOBAL loaded = LoadResource(g_module, iconResource);
		const DWORD size = SizeofResource(g_module, iconResource);
		const void* data = loaded ? LockResource(loaded) : nullptr;
		if (data && size) {
			std::ofstream output(internalIcon, std::ios::binary | std::ios::trunc);
			output.write(static_cast<const char*>(data), size);
		}
	}
	std::filesystem::copy_file(internalIcon, publicIcon,
		std::filesystem::copy_options::overwrite_existing,
		error);
}

CStableAudio3Plugin::~CStableAudio3Plugin() {
	Cancel();
	JoinWorker();
}
void CStableAudio3Plugin::JoinWorker() {
	std::lock_guard<std::mutex> lock(workerMutex);
	if (worker.joinable())
		worker.join();
}
void CStableAudio3Plugin::Cancel() { cancel = true; }
void CStableAudio3Plugin::PostStatus(const std::wstring& t, int p, bool f,
	bool s) {
	if (window && IsWindow(window))
		PostMessageW(window, WM_SA3_STATUS, 0, (LPARAM) new UiStatus{ t, p, f, s });
}
void CStableAudio3Plugin::StartGeneration(GenerationOptions o) {
	if (busy.exchange(true))
		return;
	JoinWorker();
	cancel = false;
	worker = std::thread([this, o = std::move(o)]{
	  auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::system_clock::now().time_since_epoch())
					   .count();
	  lastOutput =
		  workDirectory / (L"stable-audio-3-" + std::to_wstring(stamp) + L".wav");
	  std::wstring e;

	  bool ok = GenerateAudio(
		  *this, o, lastOutput, [this](auto& s, int p) { PostStatus(s, p); }, e);
	  busy = false;
	  PostStatus(ok ? L"Generation completed" : e, ok ? 100 : -1, true, ok);
		});
}
void CStableAudio3Plugin::StartDownload(std::wstring m, std::wstring e) {
	if (busy.exchange(true))
		return;
	JoinWorker();
	cancel = false;
	worker = std::thread([this, m = std::move(m), e = std::move(e)]{
	  std::wstring error;
	  bool ok = DownloadModelSet(
		  *this, m, e, [this](auto& s, int p) { PostStatus(s, p); }, error);
	  busy = false;
	  PostStatus(ok ? L"Model is ready" : error, ok ? 100 : -1, true, false);
		});
}

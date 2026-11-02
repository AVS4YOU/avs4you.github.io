#include "../../../sdk/common/utils.h"
#include "../../../sdk/include/AVSConsts.h"
#include "../../../sdk/translate/translate.h"
#include "export_utils.h"
#include "plugin.h"
#include "ui.h"
#include <string>

namespace {
	// first file of ClickMenuItemEx params: {"version":1,"context":2,"files":["..."]}
	std::filesystem::path FirstFileFromParams(const wchar_t* params) {
		if (!params)
			return {};
		const nlohmann::json json = nlohmann::json::parse(NSStringUtils::wstring_to_utf8(params), nullptr, false);
		if (json.is_discarded() || !json.is_object())
			return {};
		const auto files = json.find("files");
		if (files == json.end() || !files->is_array() || files->empty() || !files->front().is_string())
			return {};
		return std::filesystem::path(NSStringUtils::utf8_to_wstring(files->front().get<std::string>()));
	}
} // namespace

extern "C" {
	PluginHandle __stdcall CreatePlugin() { return new CSubtitlesPlugin(); }
	void __stdcall DeletePlugin(PluginHandle plugin) { delete static_cast<CSubtitlesPlugin*>(plugin); }
	Plugins::PluginType __stdcall PluginType() { return Plugins::PluginType::Content; }
	wchar_t* __stdcall PluginId() { return export_str(L"AiSubtitles.plugin"); }
	wchar_t* __stdcall PluginName() { return export_str(Translate(L"AI Subtitles").c_str()); }
	wchar_t* __stdcall PluginVersion() { return export_str(L"1.0.1"); }
	wchar_t* __stdcall PluginIcon(PluginHandle plugin) {
		return export_str((static_cast<CSubtitlesPlugin*>(plugin)->workDirectory / L"icon.ico").c_str());
	}
	bool __stdcall IsApplicationSupported(int id) {
		return id == AVS_VIDEO_EDITOR || id == AVS_AUDIO_EDITOR || id == AVS_VIDEO_CONVERTER ||
			id == AVS_VIDEO_REMAKER || id == AVS_AUDIO_CONVERTER;
	}
	void __stdcall ReleasePluginString(wchar_t* value) { release_export_ptr(value); }
	void __stdcall SetLanguage(PluginHandle, const wchar_t* name) {
		CTranslate::GetInstance().GetManager()->SetLang(NSStringUtils::wstring_to_utf8(name));
	}
	wchar_t* __stdcall GetMenuForContext(PluginHandle, Plugins::ContextType context) {
		nlohmann::json json = nlohmann::json::array();
		if (context == Plugins::ContextType::MediaLibrary || context == Plugins::ContextType::Video ||
			context == Plugins::ContextType::Audio) {
			nlohmann::json item;
			item["text"] = NSStringUtils::wstring_to_utf8(Translate(L"AI Subtitles"));
			item["icon"] = 0;
			item["action"] = 0;
			json.push_back(item);
		}
		return export_str(NSStringUtils::utf8_to_wstring(json.dump()).c_str());
	}
	wchar_t* __stdcall GetPluginMenu(PluginHandle plugin) {
		return GetMenuForContext(plugin, Plugins::ContextType::MediaLibrary);
	}
	wchar_t* __stdcall GetIconById(PluginHandle plugin, int) { return PluginIcon(plugin); }
	void __stdcall ClickMenuItem(PluginHandle plugin, int) {
		ShowSubtitlesWindow(static_cast<CSubtitlesPlugin*>(plugin));
	}
	void __stdcall ClickMenuItemEx(PluginHandle plugin, int, const wchar_t* params) {
		ShowSubtitlesWindow(static_cast<CSubtitlesPlugin*>(plugin), FirstFileFromParams(params));
	}
	void __stdcall SetCallbackHandler(PluginHandle plugin, AsyncCallback callback, void* context) {
		auto* subtitles = static_cast<CSubtitlesPlugin*>(plugin);
		subtitles->callback = callback;
		subtitles->callbackContext = context;
	}
	void __stdcall CleanTemporaryFiles(PluginHandle) {}
	wchar_t* __stdcall PluginInfo(PluginHandle) {
		return export_str(L"Create subtitles for any video or audio file with Whisper, on this computer.");
	}
}

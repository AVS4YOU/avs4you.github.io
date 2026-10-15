#include "../../../sdk/common/utils.h"
#include "../../../sdk/include/AVSConsts.h"
#include "../../../sdk/translate/translate.h"
#include "export_utils.h"
#include "plugin.h"
#include "ui.h"
#include <string>

extern "C" {
	PluginHandle __stdcall CreatePlugin() { return new CSubtitlesPlugin(); }
	void __stdcall DeletePlugin(PluginHandle plugin) { delete static_cast<CSubtitlesPlugin*>(plugin); }
	Plugins::PluginType __stdcall PluginType() { return Plugins::PluginType::Content; }
	wchar_t* __stdcall PluginId() { return export_str(L"AiSubtitles.plugin"); }
	wchar_t* __stdcall PluginName() { return export_str(Translate(L"AI Subtitles").c_str()); }
	wchar_t* __stdcall PluginVersion() { return export_str(L"1.0.0"); }
	wchar_t* __stdcall PluginIcon(PluginHandle plugin) {
		return export_str((static_cast<CSubtitlesPlugin*>(plugin)->workDirectory / L"icon.ico").c_str());
	}
	bool __stdcall IsApplicationSupported(int id) {
		return id == AVS_VIDEO_EDITOR || id == AVS_AUDIO_EDITOR || id == AVS_VIDEO_CONVERTER;
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

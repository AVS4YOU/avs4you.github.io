#include "../../../sdk/common/utils.h"
#include "../../../sdk/include/AVSConsts.h"
#include "../../../sdk/translate/translate.h"
#include "export_utils.h"
#include "plugin.h"
#include "ui.h"
#include <string>

extern "C" {
	PluginHandle __stdcall CreatePlugin() { return new CStemSplitterPlugin(); }
	void __stdcall DeletePlugin(PluginHandle plugin) { delete static_cast<CStemSplitterPlugin*>(plugin); }
	Plugins::PluginType __stdcall PluginType() { return Plugins::PluginType::Content; }
	wchar_t* __stdcall PluginId() { return export_str(L"StemSplitter.plugin"); }
	wchar_t* __stdcall PluginName() { return export_str(Translate(L"Stem Splitter").c_str()); }
	wchar_t* __stdcall PluginVersion() { return export_str(L"1.0.0"); }
	wchar_t* __stdcall PluginIcon(PluginHandle plugin) {
		return export_str((static_cast<CStemSplitterPlugin*>(plugin)->workDirectory / L"icon.ico").c_str());
	}
	bool __stdcall IsApplicationSupported(int id) {
		return id == AVS_AUDIO_EDITOR || id == AVS_VIDEO_EDITOR;
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
			item["text"] = NSStringUtils::wstring_to_utf8(Translate(L"Stem Splitter"));
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
		ShowStemSplitterWindow(static_cast<CStemSplitterPlugin*>(plugin));
	}
	void __stdcall SetCallbackHandler(PluginHandle plugin, AsyncCallback callback, void* context) {
		auto* splitter = static_cast<CStemSplitterPlugin*>(plugin);
		splitter->callback = callback;
		splitter->callbackContext = context;
	}
	void __stdcall CleanTemporaryFiles(PluginHandle) {}
	wchar_t* __stdcall PluginInfo(PluginHandle) {
		return export_str(L"Separate a song into vocals, drums, bass and more with Demucs, on this computer.");
	}
}

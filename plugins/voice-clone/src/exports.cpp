#include "../../../sdk/common/utils.h"
#include "../../../sdk/include/AVSConsts.h"
#include "../../../sdk/translate/translate.h"
#include "export_utils.h"
#include "plugin.h"
#include "ui.h"
#include <string>

namespace {
	// first file of ClickMenuItemEx params: {"version":1,"context":3,"files":["..."]}
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
	PluginHandle __stdcall CreatePlugin() { return new CVoiceClonePlugin(); }
	void __stdcall DeletePlugin(PluginHandle plugin) { delete static_cast<CVoiceClonePlugin*>(plugin); }
	Plugins::PluginType __stdcall PluginType() { return Plugins::PluginType::Content; }
	wchar_t* __stdcall PluginId() { return export_str(L"VoiceClone.plugin"); }
	wchar_t* __stdcall PluginName() { return export_str(Translate(L"Voice Clone").c_str()); }
	wchar_t* __stdcall PluginVersion() { return export_str(L"1.0.0"); }
	wchar_t* __stdcall PluginIcon(PluginHandle plugin) {
		return export_str((static_cast<CVoiceClonePlugin*>(plugin)->workDirectory / L"icon.ico").c_str());
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
			item["text"] = NSStringUtils::wstring_to_utf8(Translate(L"Voice Clone"));
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
		ShowVoiceCloneWindow(static_cast<CVoiceClonePlugin*>(plugin));
	}
	void __stdcall ClickMenuItemEx(PluginHandle plugin, int, const wchar_t* params) {
		ShowVoiceCloneWindow(static_cast<CVoiceClonePlugin*>(plugin), FirstFileFromParams(params));
	}
	void __stdcall SetCallbackHandler(PluginHandle plugin, AsyncCallback callback, void* context) {
		auto* voiceClone = static_cast<CVoiceClonePlugin*>(plugin);
		voiceClone->callback = callback;
		voiceClone->callbackContext = context;
	}
	void __stdcall CleanTemporaryFiles(PluginHandle) {}
	wchar_t* __stdcall PluginInfo(PluginHandle) {
		return export_str(L"Sing or speak in another voice from a short sample, with Seed-VC, on this computer.");
	}
}

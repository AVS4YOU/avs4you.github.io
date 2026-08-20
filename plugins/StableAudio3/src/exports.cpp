#include "../../../sdk/include/AVSConsts.h"
#include "export_utils.h"
#include "plugin.h"
#include "ui.h"
#include <string>
extern "C" {
	PluginHandle __stdcall CreatePlugin() { return new CStableAudio3Plugin(); }
	void __stdcall DeletePlugin(PluginHandle p) { delete (CStableAudio3Plugin*)p; }
	Plugins::PluginType __stdcall PluginType() {
		return Plugins::PluginType::Content;
	}
	wchar_t* __stdcall PluginId() { return export_str(L"StableAudio3.plugin"); }
	wchar_t* __stdcall PluginName() { return export_str(L"Stable Audio 3"); }
	wchar_t* __stdcall PluginVersion() { return export_str(L"1.0.0"); }
	wchar_t* __stdcall PluginIcon(PluginHandle p) {
		auto* plugin = static_cast<CStableAudio3Plugin*>(p);
		return export_str((plugin->workDirectory / L"icon.ico").c_str());
	}
	bool __stdcall IsApplicationSupported(int id) {
		return id == AVS_AUDIO_EDITOR || id == AVS_VIDEO_CONVERTER ||
			id == AVS_VIDEO_EDITOR;
	}
	void __stdcall ReleasePluginString(wchar_t* p) { release_export_ptr(p); }
	void __stdcall SetLanguage(PluginHandle, const wchar_t*) {}
	wchar_t* __stdcall GetMenuForContext(PluginHandle, Plugins::ContextType t) {
		return export_str(
			t == Plugins::ContextType::MediaLibrary
			? L"[{\"text\":\"Stable Audio 3\",\"icon\":0,\"action\":0}]"
			: L"[]");
	}
	wchar_t* __stdcall GetPluginMenu(PluginHandle p) {
		return GetMenuForContext(p, Plugins::ContextType::MediaLibrary);
	}
	wchar_t* __stdcall GetIconById(PluginHandle p, int) { return PluginIcon(p); }
	void __stdcall ClickMenuItem(PluginHandle p, int) {
		ShowStableAudioWindow((CStableAudio3Plugin*)p);
	}
	void __stdcall SetCallbackHandler(PluginHandle p, AsyncCallback cb, void* c) {
		auto* x = (CStableAudio3Plugin*)p;
		x->callback = cb;
		x->callbackContext = c;
	}
	void __stdcall CleanTemporaryFiles(PluginHandle) {}
	wchar_t* __stdcall PluginInfo(PluginHandle) {
		return export_str(
			L"Generate audio locally with Stable Audio 3 using CPU or CUDA.");
	}
}

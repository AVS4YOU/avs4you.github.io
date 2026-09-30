#include "pch.h"
#include "plugin.h"
#include "export_utils.h"
#include "../../../sdk/include/AVSConsts.h"
#include "../../../sdk/common/utils.h"
#include "../../../sdk/translate/translate.h"
extern "C" {
__declspec(dllexport) PluginHandle __stdcall CreatePlugin() { return new SupertonicPlugin; }
__declspec(dllexport) void __stdcall DeletePlugin(PluginHandle p) { delete static_cast<SupertonicPlugin*>(p); }
__declspec(dllexport) Plugins::PluginType __stdcall PluginType() { return Plugins::PluginType::Content; }
__declspec(dllexport) wchar_t* __stdcall PluginId() { return ExportString(L"Supertonic3.plugin"); }
__declspec(dllexport) wchar_t* __stdcall PluginName() { return ExportString(CTranslate::GetInstance().GetManager()->Translate(L"Supertonic 3").c_str()); }
__declspec(dllexport) wchar_t* __stdcall PluginVersion() { return ExportString(L"1.0.0"); }
__declspec(dllexport) wchar_t* __stdcall PluginIcon(PluginHandle p) { auto* plugin = static_cast<SupertonicPlugin*>(p); return plugin ? ExportString((plugin->workDirectory / L"icon.ico").c_str()) : nullptr; }
__declspec(dllexport) bool __stdcall IsApplicationSupported(int id) { return id == AVS_AUDIO_EDITOR || id == AVS_VIDEO_EDITOR; }
__declspec(dllexport) void __stdcall ReleasePluginString(wchar_t* p) { ReleaseExportString(p); }
__declspec(dllexport) void __stdcall SetLanguage(PluginHandle, const wchar_t* name) { if (name) CTranslate::GetInstance().GetManager()->SetLang(NSStringUtils::wstring_to_utf8(name)); }
__declspec(dllexport) void __stdcall SetParentWindow(PluginHandle p, void* hwnd) { static_cast<SupertonicPlugin*>(p)->parentWindow = static_cast<HWND>(hwnd); }
__declspec(dllexport) void __stdcall SetTemporaryPath(PluginHandle, wchar_t*) {}
__declspec(dllexport) void __stdcall CleanTemporaryFiles(PluginHandle) {}
__declspec(dllexport) void __stdcall CleanCachedData(PluginHandle) {}
__declspec(dllexport) wchar_t* __stdcall GetMenuForContext(PluginHandle, Plugins::ContextType t) { if (t != Plugins::ContextType::MediaLibrary) return ExportString(L"[]"); nlohmann::json menu = nlohmann::json::array(); menu.push_back({{"text", NSStringUtils::wstring_to_utf8(CTranslate::GetInstance().GetManager()->Translate(L"Supertonic 3"))}, {"icon", 0}, {"action", 0}}); return ExportString(NSStringUtils::utf8_to_wstring(menu.dump()).c_str()); }
__declspec(dllexport) wchar_t* __stdcall GetPluginMenu(PluginHandle p) { return GetMenuForContext(p, Plugins::ContextType::MediaLibrary); }
__declspec(dllexport) wchar_t* __stdcall GetIconById(PluginHandle p, int) { return PluginIcon(p); }
__declspec(dllexport) void __stdcall ClickMenuItem(PluginHandle p, int) { ShowSupertonicWindow(static_cast<SupertonicPlugin*>(p)); }
__declspec(dllexport) void __stdcall SetCallbackHandler(PluginHandle p, AsyncCallback cb, void* context) { auto* x = static_cast<SupertonicPlugin*>(p); x->callback = cb; x->callbackContext = context; }
__declspec(dllexport) wchar_t* __stdcall PluginInfo(PluginHandle) { return ExportString(CTranslate::GetInstance().GetManager()->Translate(L"Generate speech locally with Supertonic 3 through ONNX Runtime.").c_str()); }
}





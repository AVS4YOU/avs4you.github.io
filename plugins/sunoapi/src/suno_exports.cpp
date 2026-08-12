#include "../../../sdk/3dparty/nlohmann/json/single_include/nlohmann/json.hpp"
#include "../../../sdk/include/AVSConsts.h"
#include "../../../sdk/include/CContentPluginIntf.h"
#include "export_utils.h"
#include "suno_api.h"
#include "suno_plugin.h"
#include "suno_ui.h"
extern "C"
{
	PluginHandle __stdcall CreatePlugin()
	{
		return new CSunoApiPlugin();
	}

	void __stdcall DeletePlugin(PluginHandle p)
	{
		delete (CSunoApiPlugin *)p;
	}

	Plugins::PluginType __stdcall PluginType()
	{
		return Plugins::PluginType::Content;
	}

	wchar_t *__stdcall PluginId()
	{
		return export_str(L"SunoApi.plugin");
	}

	wchar_t *__stdcall PluginName()
	{
		return export_str(L"Suno API");
	}

	wchar_t *__stdcall PluginVersion()
	{
		return export_str(L"1.0.0");
	}

	wchar_t *__stdcall PluginIcon(PluginHandle p)
	{
		CSunoApiPlugin *plugin = (CSunoApiPlugin *)p;
		return export_str((plugin->workDirectory + L"\\icon.ico").c_str());
	}

	bool __stdcall IsApplicationSupported(int id)
	{
		return id == AVS_AUDIO_EDITOR || id == AVS_AUDIO_CONVERTER;
	}

	void __stdcall ReleasePluginString(wchar_t *p)
	{
		release_export_ptr(p);
	}

	void __stdcall SetLanguage(PluginHandle, const wchar_t *)
	{
	}

	wchar_t *__stdcall GetMenuForContext(PluginHandle, Plugins::ContextType type)
	{
		nlohmann::json a = nlohmann::json::array();
		if (type == Plugins::ContextType::MediaLibrary)
			a.push_back({{"text", "Suno API"}, {"icon", 0}, {"action", 0}});
		return export_str(Suno::Utf8ToWide(a.dump()).c_str());
	}

	wchar_t *__stdcall GetPluginMenu(PluginHandle p)
	{
		return GetMenuForContext(p, Plugins::ContextType::MediaLibrary);
	}

	wchar_t *__stdcall GetIconById(PluginHandle p, int)
	{
		return PluginIcon(p);
	}

	void __stdcall ClickMenuItem(PluginHandle p, int)
	{
		SunoUI::Show((CSunoApiPlugin *)p);
	}

	void __stdcall SetCallbackHandler(PluginHandle p, AsyncCallback cb, void *context)
	{
		auto x = (CSunoApiPlugin *)p;
		x->callback = cb;
		x->callbackContext = context;
	}

	void __stdcall SetParentWindow(PluginHandle p, void *w)
	{
		((CSunoApiPlugin *)p)->parentWindow = (HWND)w;
	}

	void __stdcall SetTemporaryPath(PluginHandle, wchar_t *)
	{
	}

	void __stdcall CleanTemporaryFiles(PluginHandle)
	{
	}

	void __stdcall CleanCachedData(PluginHandle)
	{
	}

	wchar_t *__stdcall PluginInfo(PluginHandle)
	{
		return export_str(L"Suno API: music, lyrics, sounds, extend, cover and vocals. Native WinHTTP client.");
	}

	wchar_t *__stdcall SunoApiExecuteJson(const wchar_t *apiKey, const wchar_t *method, const wchar_t *endpoint, const wchar_t *jsonBody)
	{
		auto r = Suno::Request(apiKey ? apiKey : L"", method ? method : L"GET", endpoint ? endpoint : L"/", jsonBody ? Suno::WideToUtf8(jsonBody) : "");
		nlohmann::json out = {{"httpStatus", r.status}, {"body", r.body}, {"error", Suno::WideToUtf8(r.error)}};
		return export_str(Suno::Utf8ToWide(out.dump()).c_str());
	}
}

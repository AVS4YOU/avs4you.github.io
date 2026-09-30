#include "export_utils.h"
#include "exports.h"

#include "../../../sdk/translate/translate.h"
#include "../../../sdk/common/utils.h"

#include "plugin.h"
#include "ui.h"

extern "C" {
	PluginHandle __stdcall CreatePlugin()
	{
		return new CSoraPlugin();
	}
	void __stdcall DeletePlugin(PluginHandle p)
	{
		if (p)
			delete p;
	}

	Plugins::PluginType __stdcall PluginType()
	{
		return Plugins::PluginType::Content;
	}

	wchar_t* __stdcall PluginId()
	{
		return export_str(L"Sora2.plugin");
	}

	wchar_t* __stdcall PluginName()
	{
		return TR(L"Sora2");
	}

	wchar_t* __stdcall PluginVersion()
	{
		return export_str(L"1.0.0");
	}

	wchar_t* __stdcall PluginIcon(PluginHandle plugin)
	{
		CSoraPlugin* pluginSora = (CSoraPlugin*)plugin;
		std::wstring iconPath = pluginSora->m_workDirectory + L"\\icon.ico";
		return export_str(iconPath.c_str());
	}

	bool __stdcall IsApplicationSupported(int id)
	{
		switch (id)
		{
		case AVS_VIDEO_CONVERTER:
		case AVS_VIDEO_EDITOR:
			return true;
		default:
			break;
		}

		return false;
	}

	void __stdcall ReleasePluginString(wchar_t* ptr)
	{
		release_export_ptr(ptr);
	}

	void __stdcall SetLanguage(PluginHandle, const wchar_t* name)
	{
		CTranslate::GetInstance().GetManager()->SetLang(NSStringUtils::wstring_to_utf8(name));
	}

	wchar_t* __stdcall GetMenuForContext(PluginHandle, Plugins::ContextType type)
	{
		nlohmann::json json = {};
		if (Plugins::ContextType::MediaLibrary == type)
		{
			nlohmann::json obj;
			obj["text"] = NSStringUtils::wstring_to_utf8(CTranslate::GetInstance().GetManager()->Translate(L"Sora2"));
			obj["icon"] = 0;
			obj["action"] = 0;

			json.push_back(obj);
		}

		std::string value = json.dump();
		std::wstring valueW = NSStringUtils::utf8_to_wstring(value);

		return export_str(valueW.c_str());
	}

	wchar_t* __stdcall GetPluginMenu(PluginHandle handle)
	{
		return GetMenuForContext(handle, Plugins::ContextType::MediaLibrary);
	}

	wchar_t* __stdcall GetIconById(PluginHandle plugin, int id)
	{
		CSoraPlugin* pluginSora = (CSoraPlugin*)plugin;
		std::wstring iconPath = pluginSora->m_workDirectory + L"\\icon.ico";
		return export_str(iconPath.c_str());
	}

	void __stdcall SetCallbackHandler(PluginHandle plugin, AsyncCallback callback, void* context)
	{
		CSoraPlugin* pluginSora = (CSoraPlugin*)plugin;
		pluginSora->m_callback = callback;
		pluginSora->m_callbackContext = context;
	}

	void __stdcall SetParentWindow(PluginHandle plugin, void* hwnd)
	{
		CSoraPlugin* pluginSora = (CSoraPlugin*)plugin;
		pluginSora->m_hParentWindow = (HWND)hwnd;
	}

	void __stdcall CleanTemporaryFiles(PluginHandle plugin)
	{
		CSoraPlugin* pluginSora = (CSoraPlugin*)plugin;
		if (NSSystemUtils::ExistsFile(pluginSora->m_workDirectory + L"\\script.ps1"))
			NSSystemUtils::RemoveFile(pluginSora->m_workDirectory + L"\\script.ps1");
	}

	void __stdcall ClickMenuItem(PluginHandle handle, int id)
	{
		NSUI::ShowPromptWindow((CSoraPlugin*)handle);
	}

	wchar_t* __stdcall PluginInfo(PluginHandle plugin)
	{
		std::wstring info = L"Transform text into immersive videos. Animate stories, visualize ideas, and bring your concepts to life";
		return export_str(info.c_str());
	}
}

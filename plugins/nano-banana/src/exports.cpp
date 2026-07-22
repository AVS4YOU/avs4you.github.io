#include "export_utils.h"
#include "exports.h"

#include "../../../sdk/translate/translate.h"
#include "../../../sdk/common/utils.h"

#include "plugin.h"
#include "nano-banana_ui.h"

extern "C" {
	PluginHandle __stdcall CreatePlugin()
	{
		return new CNanoBananaPlugin();
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
		return export_str(L"NanoBanana.plugin");
	}

	wchar_t* __stdcall PluginName()
	{
		return TR(L"Nano Banana");
	}

	wchar_t* __stdcall PluginVersion()
	{
		return export_str(L"1.0.0");
	}

	wchar_t* __stdcall PluginIcon(PluginHandle plugin)
	{
		CNanoBananaPlugin* pluginNanoBanana = (CNanoBananaPlugin*)plugin;
		std::wstring iconPath = pluginNanoBanana->m_workDirectory + L"\\icon.ico";
		return export_str(iconPath.c_str());
	}

	bool __stdcall IsApplicationSupported(int id)
	{
		switch (id)
		{
		case AVS_VIDEO_EDITOR:
		case AVS_PHOTO_EDITOR:
		case AVS_IMAGE_CONVERTER:
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
			obj["text"] = NSStringUtils::wstring_to_utf8(CTranslate::GetInstance().GetManager()->Translate(L"Nano Banana"));
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
		CNanoBananaPlugin* pluginNanoBanana = (CNanoBananaPlugin*)plugin;
		std::wstring iconPath = pluginNanoBanana->m_workDirectory + L"\\icon.ico";
		return export_str(iconPath.c_str());
	}

	void __stdcall SetCallbackHandler(PluginHandle plugin, AsyncCallback callback, void* context)
	{
		CNanoBananaPlugin* pluginNanoBanana = (CNanoBananaPlugin*)plugin;
		pluginNanoBanana->m_callback = callback;
		pluginNanoBanana->m_callbackContext = context;
	}

	void __stdcall SetParentWindow(PluginHandle plugin, void* hwnd)
	{
		CNanoBananaPlugin* pluginNanoBanana = (CNanoBananaPlugin*)plugin;
		pluginNanoBanana->m_hParentWindow = (HWND)hwnd;
	}

	void __stdcall SetTemporaryPath(PluginHandle plugin, wchar_t* path)
	{
		// Keep plugin-owned files in LocalAppData. Some hosts pass protected
		// install directories here, which breaks script logs and image output.
		(void)plugin;
		(void)path;
	}

	void __stdcall CleanTemporaryFiles(PluginHandle plugin)
	{
		CNanoBananaPlugin* pluginNanoBanana = (CNanoBananaPlugin*)plugin;
		if (NSSystemUtils::ExistsFile(pluginNanoBanana->m_workDirectory + L"\\script.ps1"))
			NSSystemUtils::RemoveFile(pluginNanoBanana->m_workDirectory + L"\\script.ps1");
	}

	void __stdcall CleanCachedData(PluginHandle plugin)
	{
		CleanTemporaryFiles(plugin);
	}

	void __stdcall ClickMenuItem(PluginHandle handle, int id)
	{
		NSUI::ShowPromptWindow((CNanoBananaPlugin*)handle);
	}

	wchar_t* __stdcall PluginInfo(PluginHandle plugin)
	{
		std::wstring info = L"Generate and edit images with Nano Banana models through the Gemini API.";
		return export_str(info.c_str());
	}
}

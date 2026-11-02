#include "IHeygenPlugin.h"
#include "CBase.h"
#include "utils.h"
#include "defines.h"
#include "AVSConsts.h"
#include "CHeygenPlugin.h"

#ifdef __cplusplus
extern "C" {
#endif
    PLUGIN_API PluginHandle __stdcall CreatePlugin()
    {
        return new CHeygenPlugin;
    }

    PLUGIN_API void __stdcall DeletePlugin(PluginHandle ptr)
    {
        if (ptr) {
            delete static_cast<CHeygenPlugin*>(ptr), ptr = nullptr;
        }
    }

	PLUGIN_API Plugins::PluginType __stdcall PluginType()
    {
        return Plugins::PluginType::Content;
    }

    PLUGIN_API wchar_t* __stdcall PluginId()
    {
		return utils::wstr_to_ptr(PLUGIN_ID);
    }

    PLUGIN_API wchar_t* __stdcall PluginName()
    {
		return utils::wstr_to_ptr(L"HeyGen");
    }

    PLUGIN_API wchar_t* __stdcall PluginVersion()
    {
 		return utils::wstr_to_ptr(L"1.0.2");
   }

    PLUGIN_API wchar_t* __stdcall PluginIcon(PluginHandle h)
    {
        if ( h ) {
            CHeygenPlugin* ptr = static_cast<CHeygenPlugin*>(h);
            if ( ptr ) {
                return utils::wstr_to_ptr(ptr->Icon());
            }
        }

        return NULL;
    }

    PLUGIN_API bool __stdcall IsApplicationSupported(int id)
    {
    	return AVS_VIDEO_EDITOR == id || AVS_VIDEO_CONVERTER == id;
    }

	PLUGIN_API void __stdcall ReleasePluginString(wchar_t* ptr)
    {
        delete[] ptr;
    }

	PLUGIN_API void __stdcall SetLanguage(PluginHandle h, const wchar_t* l)
    {
        if ( h ) {
            CHeygenPlugin* ptr = static_cast<CHeygenPlugin*>(h);
            if ( ptr ) {
                ptr->SetLanguage(l);
            }
        }
    }

	PLUGIN_API wchar_t* __stdcall GetMenuForContext(PluginHandle h, Plugins::ContextType t)
    {
        if ( h ) {
            CHeygenPlugin* ptr = static_cast<CHeygenPlugin*>(h);
            if ( ptr ) {
                return utils::wstr_to_ptr(ptr->GetMenuForContext(t));
            }
        }

        return NULL;
    }

	PLUGIN_API wchar_t* __stdcall GetPluginMenu(PluginHandle h)
    {
        if ( h ) {
            CHeygenPlugin* ptr = static_cast<CHeygenPlugin*>(h);
            if ( ptr ) {
				return utils::wstr_to_ptr(ptr->GetPluginMenu());
			}
		}

		return NULL;
	}

	PLUGIN_API wchar_t* __stdcall GetIconById(PluginHandle h, int id)
	{
		if ( h ) {
			CHeygenPlugin* ptr = static_cast<CHeygenPlugin*>(h);
			if ( ptr ) {
                return utils::wstr_to_ptr(ptr->GetIconById(id));
            }
        }

        return NULL;
    }

	PLUGIN_API void __stdcall ClickMenuItem(PluginHandle h, int id)
    {
        if ( h ) {
            CHeygenPlugin* ptr = static_cast<CHeygenPlugin*>(h);
            if ( ptr ) {
                ptr->ClickMenuItem(id);
            }
        }
    }

    PLUGIN_API void __stdcall SetCallbackHandler(PluginHandle h, AsyncCallback fn, void* data)
    {
        if ( h ) {
            CHeygenPlugin* ptr = static_cast<CHeygenPlugin*>(h);
            if ( ptr ) {
                ptr->SetCallbackHandler(fn, data);
            }
        }
    }

    PLUGIN_API void __stdcall CleanTemporaryFiles(PluginHandle h)
    {
        if ( h ) {
            CHeygenPlugin* ptr = static_cast<CHeygenPlugin*>(h);
            if ( ptr ) {
                ptr->CleanTemporaryFiles();
            }
        }
    }


#ifdef __cplusplus
}
#endif

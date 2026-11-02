// dllmain.cpp : Defines the entry point for the DLL application.
#define PLUGIN_EXPORTS
// #define __cplusplus
#include "./onnx.h"
#include "./export_utils.h"
#include <iostream>

std::wstring GetDllPath()
{
    HMODULE hModule = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&GetDllPath,
        &hModule
    );

    DWORD size = 256;
    std::wstring result;

    while (true)
    {
        result.resize(size);
        DWORD len = GetModuleFileNameW(hModule, (LPWSTR)result.data(), size);

        if (len == 0)
            break;

        if (len < size - 1)
        {
            result.resize(len);
            break;
        }

        size *= 2;
    }

    size_t pos = result.find_last_of(L"\\/");

    if (pos != std::string::npos)
        return result.substr(0, pos);

    return result;
}

extern "C" {
    PLUGIN_API void __stdcall ReleasePluginString(wchar_t* ptr)
    {
        release_export_ptr(ptr);
    }

    PLUGIN_API Plugins::PluginType __stdcall PluginType()
    {
        return Plugins::PluginType::ImageEffect;
    }

    PLUGIN_API wchar_t* __stdcall PluginId()
    {
        return export_str(L"EffectGaterv3.plugin");
    }

    PLUGIN_API wchar_t* __stdcall PluginName()
    {
        return export_str(L"Gaterv3");
    }

    PLUGIN_API wchar_t* __stdcall PluginVersion()
    {
        return export_str(L"1.0.2");
    }

    PLUGIN_API wchar_t* __stdcall PluginIcon()
    {
        return export_str(L"");
    }

    PLUGIN_API bool __stdcall IsApplicationSupported(int id)
    {
        switch (id)
        {
        case AVS_VIDEO_CONVERTER:
        case AVS_VIDEO_EDITOR:
        case AVS_IMAGE_CONVERTER:
            return true;
        default:break;
        }
        return false;
    }

    PLUGIN_API int __stdcall GetEffectsCount()
    {
        return 1;
    }

    //    PLUGIN_API int __stdcall GetEffectId(int) { return 0; }

    PLUGIN_API wchar_t* __stdcall GetEffectName(int)
    {
        return export_str(L"Gaterv3");
    }

    PLUGIN_API wchar_t* __stdcall GetEffectParams(int)
    {
        return NULL;
    }

    PLUGIN_API HRESULT __stdcall ApplyEffect(BYTE* pPixels, int w, int h, double dCompleteness, int nID, const BSTR* sParams, void** p)
    {
        if (!*p)
        {
            std::wstring model = L"1xgaterv3_r_restore_fp32_op17.onnx";
            std::wstring path = GetDllPath() + L"\\" + model;
            *p = new COnnx(path, 8);
        }

        COnnx* onnx = static_cast<COnnx*>(*p);
        onnx->Apply(pPixels, w, h, 1024);
        return S_OK;
    }
    PLUGIN_API void __stdcall ReleaseEffectData(void* data)
    {
        if (data)
        {
            COnnx* onnx = static_cast<COnnx*>(data);
            delete onnx;
        }
    }
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}


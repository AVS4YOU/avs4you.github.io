// dllmain.cpp : Defines the entry point for the DLL application.
#define PLUGIN_EXPORTS
//#define __cplusplus

#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
#include "../../sdk/include/CEffectPluginIntf.h"
#include "../../sdk/include/AVSConsts.h"

#include <cmath>

WCHAR* export_str(const WCHAR* ptr)
{
    if (ptr == NULL)
        return NULL;

    size_t len = wcslen(ptr);
    WCHAR* result = new WCHAR[len + 1];

    if (result == NULL)
        return NULL;

    wcscpy_s(result, len + 1, ptr);

    return result;
}
void release_export_ptr(const WCHAR* ptr)
{
    delete[] ptr;
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
        return export_str(L"EffectSpiral.plugin");
    }

    PLUGIN_API wchar_t* __stdcall PluginName()
    {
        return export_str(L"Spiral");
    }

    PLUGIN_API wchar_t* __stdcall PluginVersion()
    {
        return export_str(L"1.0.3");
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
        return export_str(L"Spiral");
    }

    PLUGIN_API wchar_t* __stdcall GetEffectParams(int)
    {
        return NULL;
    }

    PLUGIN_API HRESULT __stdcall ApplyEffect(BYTE* pPixels, int w, int h, double dCompleteness, int nID, const BSTR* sParams, void** p)
    {
        size_t frame_size = (size_t)(4 * w * h);
        const float time = float(dCompleteness);
        const float cx = w * 0.5f;
        const float cy = h * 0.5f;

        // How far the spiral “explodes” outward
        float explode = time * 60.0f;

        // How strong the twisting effect is
        float twistPower = time * 4.0f;

        BYTE* out = new(std::nothrow) BYTE[frame_size];
        if (!out)
            return S_FALSE;

        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                float dx = x - cx;
                float dy = y - cy;

                float r = sqrtf(dx * dx + dy * dy);
                float angle = atan2f(dy, dx);

                // Spiral twist — angle increases with radius
                float twist = twistPower * (r / (float)w) * 3.1415926f;

                // Explosion — pixels drift outward radially
                float radial = explode * (r / (float)w);

                float newR = r + radial;
                float newAngle = angle + twist;

                int sx = int(cx + newR * cosf(newAngle));
                int sy = int(cy + newR * sinf(newAngle));

                int idx = (y * w + x) * 4;
                BYTE* dst = &out[idx];

                // Out of bounds — fade to black
                if (sx < 0 || sy < 0 || sx >= w || sy >= h)
                {
                    dst[0] = dst[1] = dst[2] = 0;
                    dst[3] = 0;
                }
                else
                {
                    const BYTE* src = &pPixels[(sy * w + sx) * 4];
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = src[3];
                }
            }
        }

        memcpy(pPixels, out, frame_size);

        delete[] out;
        return S_OK;
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


// dllmain.cpp : Defines the entry point for the DLL application.
#define PLUGIN_EXPORTS
//#define __cplusplus

#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
#include "../../sdk/include/CEffectPluginIntf.h"
#include "../../sdk/include/AVSConsts.h"

#include <cmath>
#include <cstdlib>
#include <ctime>

//#define _DEBUG_LOG
#ifdef _DEBUG_LOG
#include <iostream>
#include <gdiplus.h>
#include <string>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

class GDIPlusManager
{
private:
    ULONG_PTR gdiplusToken;
    CLSID pngClsid;

    GDIPlusManager()
    {
        GdiplusStartupInput gdiplusStartupInput;
        GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

        GetEncoderClsid(L"image/png", &pngClsid);
    }

    ~GDIPlusManager()
    {
        GdiplusShutdown(gdiplusToken);
    }

    static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
    {
        UINT num = 0, size = 0;
        GetImageEncodersSize(&num, &size);
        if (size == 0) return -1;

        ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
        if (pImageCodecInfo == NULL) return -1;

        GetImageEncoders(num, size, pImageCodecInfo);

        for (UINT i = 0; i < num; i++) 
        {
            if (wcscmp(pImageCodecInfo[i].MimeType, format) == 0) 
            {
                *pClsid = pImageCodecInfo[i].Clsid;
                free(pImageCodecInfo);
                return i;
            }
        }

        free(pImageCodecInfo);
        return -1;
    }

public:
    GDIPlusManager(const GDIPlusManager&) = delete;
    GDIPlusManager& operator=(const GDIPlusManager&) = delete;

    static GDIPlusManager& Instance()
    {
        static GDIPlusManager instance;
        return instance;
    }

    const CLSID& GetPngClsid() const { return pngClsid; }
};

bool SaveBGRAToPNG(const BYTE* pixels, int width, int height, const std::wstring& filename)
{
    int stride = width * 4;
    Bitmap bitmap(width, height, stride, PixelFormat32bppARGB, (BYTE*)pixels);

    Status status = bitmap.Save(filename.c_str(), &GDIPlusManager::Instance().GetPngClsid());
    return (status == Ok);
}

bool SaveBGRAToPNG(const BYTE* pixels, int width, int height, const std::wstring& base, const double& completeness)
{
    return SaveBGRAToPNG(pixels, width, height, base + L"_" + std::to_wstring(completeness) + L".png");
}

#endif

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

class VHSEffectData
{
public:
    double       lastGlitchTime = 0;
    int          glitchOffset = 0;
    int          glitchLine = 0;
    bool         isInitialized = false;
    unsigned int frameCount = 0;
    float        chromaShiftPhase = 0;
    int          trackingErrorOffset = 0;

    VHSEffectData() {}
};

inline float noise(int x, int y, int seed)
{
    int n = x + y * 57 + seed * 131;
    n = (n << 13) ^ n;
    return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

// Applying color distortions (de-saturation, changing shades)
inline void applyColorDegradation(BYTE& b, BYTE& g, BYTE& r, double intensity)
{
    // Convert to YUV for more realistic degradation
    float y = 0.299f * r + 0.587f * g + 0.114f * b;
    float u = (b - y) * 0.565f;
    float v = (r - y) * 0.713f;

    // Reduction of color information (like on VHS)
    u *= (1.0f - intensity * 0.3f);
    v *= (1.0f - intensity * 0.25f);

    // Adding a yellowish tint
    v += intensity * 5.0f;

    // Convert back to RGB
    float rf = y + 1.403f * v;
    float gf = y - 0.344f * u - 0.714f * v;
    float bf = y + 1.770f * u;

    // Clamp
    r = (BYTE)max(0, min(255, (int)rf));
    g = (BYTE)max(0, min(255, (int)gf));
    b = (BYTE)max(0, min(255, (int)bf));
}

// Adding horizontal stripes (tracking errors)
inline void applyTrackingNoise(BYTE* pPixels, int w, int h, int y, int offset, double intensity)
{
    if (y < 0 || y >= h) return;

    int rowStart = y * w * 4;
    for (int x = 0; x < w; x++)
    {
        int srcX = x - offset;
        if (srcX >= 0 && srcX < w)
        {
            int srcIdx = rowStart + srcX * 4;
            int dstIdx = rowStart + x * 4;

            pPixels[dstIdx] = pPixels[srcIdx];
            pPixels[dstIdx + 1] = pPixels[srcIdx + 1];
            pPixels[dstIdx + 2] = pPixels[srcIdx + 2];
        }
    }
}

// Chromatic aberration (color channel separation)
inline void applyChromaShift(BYTE* pPixels, int w, int h, int y, double intensity)
{
    if (y < 0 || y >= h) return;

    int offset = (int)(intensity * 3);
    int rowStart = y * w * 4;

    for (int x = w - 1; x >= offset; x--)
    {
        int idx = rowStart + x * 4;
        int srcIdxR = rowStart + (x - offset) * 4;

        pPixels[idx + 2] = pPixels[srcIdxR + 2]; // R channel offset
    }
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
        return export_str(L"EffectVHS.plugin");
    }

    PLUGIN_API wchar_t* __stdcall PluginName()
    {
        return export_str(L"EffectVHS");
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
        return export_str(L"VHS");
    }

    PLUGIN_API wchar_t* __stdcall GetEffectParams(int)
    {
        return NULL;
    }

    PLUGIN_API HRESULT __stdcall ApplyEffect(BYTE* pPixels, int w, int h, double dCompleteness, int nID, const BSTR* sParams, void** p)
    {
        // Initialize effect data
        VHSEffectData* data = nullptr;
        if (*p == nullptr)
        {
            data = new VHSEffectData();
            data->isInitialized = true;
            srand((unsigned int)time(nullptr));
            *p = data;
        }
        else
        {
            data = (VHSEffectData*)(*p);
        }

        data->frameCount++;

        if (dCompleteness < 0.001) return S_OK;

        double intensity = dCompleteness;

        // 1. NOISE
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                int idx = (y * w + x) * 4;
                float noiseVal = noise(x, y, data->frameCount) * intensity * 15.0f;

                pPixels[idx] = (BYTE)max(0, min(255, pPixels[idx] + (int)noiseVal));
                pPixels[idx + 1] = (BYTE)max(0, min(255, pPixels[idx + 1] + (int)noiseVal));
                pPixels[idx + 2] = (BYTE)max(0, min(255, pPixels[idx + 2] + (int)noiseVal));
            }
        }

        // 2. COLOR DEGRADATION
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                int idx = (y * w + x) * 4;
                applyColorDegradation(pPixels[idx], pPixels[idx + 1], pPixels[idx + 2], intensity);
            }
        }

        // 3. CHROMATIC ABERRATION (color channel separation)
        data->chromaShiftPhase += 0.1f;
        for (int y = 0; y < h; y++)
        {
            double wave = sin(y * 0.05 + data->chromaShiftPhase) * 0.5 + 0.5;
            applyChromaShift(pPixels, w, h, y, intensity * wave);
        }

        // 4. RANDOM GLITCHES (horizontal displacements)
        if (rand() % 100 < (int)(intensity * 2)) // 2% chance at full intensity
        {
            data->glitchLine = rand() % h;
            data->glitchOffset = (rand() % 10 - 5);
            data->lastGlitchTime = data->frameCount;
        }

        // We apply the glitch if it is active.
        if (data->frameCount - data->lastGlitchTime < 2)
        {
            int glitchHeight = 5 + rand() % 15;
            for (int i = 0; i < glitchHeight; i++)
            {
                applyTrackingNoise(pPixels, w, h, data->glitchLine + i, data->glitchOffset, intensity);
            }
        }

        // 5. TRACKING ERROR (periodic jitter)
        double trackingPhase = sin(data->frameCount * 0.05) * intensity;
        data->trackingErrorOffset = (int)(trackingPhase * 1);

        if (abs(data->trackingErrorOffset) > 0)
        {
            // Apply to several top rows
            for (int y = 0; y < min(3, h); y++)
            {
                applyTrackingNoise(pPixels, w, h, y, data->trackingErrorOffset, intensity);
            }
        }

        // 6. HORIZONTAL INTERFERENCE BANDS
        for (int i = 0; i < 3; i++)
        {
            int lineY = (data->frameCount * 3 + i * h / 3) % h;
            int rowStart = lineY * w * 4;

            for (int x = 0; x < w; x++)
            {
                int idx = rowStart + x * 4;
                float darkness = 0.7f + 0.3f * noise(x, lineY, data->frameCount);

                pPixels[idx] = (BYTE)(pPixels[idx] * darkness);
                pPixels[idx + 1] = (BYTE)(pPixels[idx + 1] * darkness);
                pPixels[idx + 2] = (BYTE)(pPixels[idx + 2] * darkness);
            }
        }

        // 7. GENERAL DARKENING AND VIGNETTING
        float centerX = w * 0.5f;
        float centerY = h * 0.5f;
        float maxDist = sqrtf(centerX * centerX + centerY * centerY);

        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                int idx = (y * w + x) * 4;

                // Vignette
                float dx = x - centerX;
                float dy = y - centerY;
                float dist = sqrtf(dx * dx + dy * dy);
                float vignette = 1.0f - (dist / maxDist) * intensity * 0.4f;

                // General darkening
                float darken = 1.0f - intensity * 0.15f;

                float factor = vignette * darken;

                pPixels[idx] = (BYTE)(pPixels[idx] * factor);
                pPixels[idx + 1] = (BYTE)(pPixels[idx + 1] * factor);
                pPixels[idx + 2] = (BYTE)(pPixels[idx + 2] * factor);
            }
        }

        return S_OK;
    }

    PLUGIN_API void __stdcall ReleaseEffectData(void* data)
    {
        if (NULL == data)
            return;

        VHSEffectData* effectData = (VHSEffectData*)data;
        delete effectData;
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


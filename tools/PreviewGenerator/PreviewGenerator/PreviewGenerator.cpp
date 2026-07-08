#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wtypes.h>
#include <oleauto.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

#include "third_party/gif/gif.h"

typedef HRESULT(__stdcall* LPAPPLYEFFECT)(
    BYTE* data,
    int width,
    int height,
    double timestamp,
    int paramCount,
    const BSTR* params,
    void** effectData
    );

typedef int(__stdcall* LPGETEFFECTSCOUNT)();
typedef const wchar_t* (__stdcall* LPGETEFFECTNAME)(int effectId);
typedef void(__stdcall* LPRELEASEPLUGINSTRING)(const wchar_t* value);
typedef void(__stdcall* LPRELEASEEFFECTDATA)(void* effectData);

static void PrintUsage()
{
    std::cout <<
        "Usage:\n"
        "  PreviewGenerator.exe effect-spiral.dll input.png output.gif [fps] [-duration seconds] [-force-completeness value]\n"
        "  PreviewGenerator.exe effect-spiral.dll input.png output.gif -side-by-side [fps] [-duration seconds] [-force-completeness value]\n"
        "  PreviewGenerator.exe effect-spiral.dll input.png output.gif -ping-pong [fps] [-duration seconds] [-force-completeness value]\n\n"
        "Examples:\n"
        "  PreviewGenerator.exe effect-spiral.dll input.png output.gif 25\n"
        "  PreviewGenerator.exe effect-spiral.dll input.png output.gif 25 -duration 2\n"
        "  PreviewGenerator.exe effect-spiral.dll input.png output.gif 25 -force-completeness 1\n"
        "  PreviewGenerator.exe effect-spiral.dll input.png comparison.gif -side-by-side 25 -duration 2\n"
        "  PreviewGenerator.exe effect-spiral.dll input.png comparison.gif -side-by-side -ping-pong 25 -duration 2\n";
}

static int GetOutputFrameCount(int frameCount, bool pingPong)
{
    if (!pingPong || frameCount <= 1)
        return frameCount;

    return frameCount * 2;
}

static int GetSourceFrameIndex(int outputFrameIndex, int frameCount, bool pingPong)
{
    if (!pingPong || frameCount <= 1 || outputFrameIndex < frameCount)
        return outputFrameIndex;

    return (frameCount - 1) - (outputFrameIndex - frameCount);
}

static std::string WideStringToUtf8(const wchar_t* value)
{
    if (!value)
        return "";

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        value,
        -1,
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (required <= 0)
        return "";

    std::string result(required, '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value,
        -1,
        &result[0],
        required,
        nullptr,
        nullptr
    );

    if (!result.empty() && result[result.size() - 1] == '\0')
        result.erase(result.size() - 1);

    return result;
}

static void BuildSideBySideFrame(
    BYTE* frame,
    const BYTE* before,
    const BYTE* after,
    int width,
    int height,
    int dividerX)
{
    const int clampedDivider = std::max(0, std::min(width, dividerX));

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const BYTE* source = (x < clampedDivider) ? before : after;
            const size_t offset = (static_cast<size_t>(y) * width + x) * 4;

            frame[offset + 0] = source[offset + 0];
            frame[offset + 1] = source[offset + 1];
            frame[offset + 2] = source[offset + 2];
            frame[offset + 3] = source[offset + 3];
        }
    }

    if (width <= 0 || height <= 0)
        return;

    const int cursorX = std::max(0, std::min(width - 1, clampedDivider));
    const int lineStart = std::max(0, cursorX - 1);
    const int lineEnd = std::min(width - 1, cursorX + 1);

    for (int y = 0; y < height; ++y)
    {
        for (int x = lineStart; x <= lineEnd; ++x)
        {
            const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
            const bool center = (x == cursorX);

            frame[offset + 0] = center ? 255 : 0;
            frame[offset + 1] = center ? 255 : 0;
            frame[offset + 2] = center ? 255 : 0;
            frame[offset + 3] = 255;
        }
    }
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        PrintUsage();
        return 1;
    }

    const char* dllPath = argv[1];
    const char* inputPng = argv[2];
    const char* outputGif = argv[3];

    bool sideBySide = false;
    bool pingPong = false;
    int fps = 25;
    double durationSeconds = 1.0;
    bool forceCompleteness = false;
    double forcedCompleteness = 0.0;

    for (int i = 4; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "-side-by-side")
        {
            sideBySide = true;
            continue;
        }

        if (arg == "-ping-pong")
        {
            pingPong = true;
            continue;
        }

        if (arg == "-duration")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after -duration.\n";
                return 1;
            }

            const double parsedDuration = atof(argv[++i]);
            if (parsedDuration > 0.0)
                durationSeconds = parsedDuration;
            else
            {
                std::cerr << "Invalid duration: " << argv[i] << "\n";
                return 1;
            }

            continue;
        }

        if (arg == "-force-completeness")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after -force-completeness.\n";
                return 1;
            }

            forcedCompleteness = atof(argv[++i]);
            forceCompleteness = true;
            continue;
        }

        const int parsedFps = atoi(argv[i]);
        if (parsedFps > 0)
            fps = parsedFps;
    }

    if (fps <= 0)
        fps = 25;

    HMODULE hDll = LoadLibraryA(dllPath);
    if (!hDll)
    {
        std::cerr << "LoadLibrary failed. Error: " << GetLastError() << "\n";
        return 1;
    }

    LPAPPLYEFFECT ApplyEffect =
        reinterpret_cast<LPAPPLYEFFECT>(GetProcAddress(hDll, "ApplyEffect"));

    if (!ApplyEffect)
    {
        std::cerr << "GetProcAddress(ApplyEffect) failed. Error: " << GetLastError() << "\n";
        FreeLibrary(hDll);
        return 1;
    }

    LPGETEFFECTSCOUNT GetEffectsCount =
        reinterpret_cast<LPGETEFFECTSCOUNT>(GetProcAddress(hDll, "GetEffectsCount"));
    LPGETEFFECTNAME GetEffectName =
        reinterpret_cast<LPGETEFFECTNAME>(GetProcAddress(hDll, "GetEffectName"));
    LPRELEASEPLUGINSTRING ReleasePluginString =
        reinterpret_cast<LPRELEASEPLUGINSTRING>(GetProcAddress(hDll, "ReleasePluginString"));
    LPRELEASEEFFECTDATA ReleaseEffectData =
        reinterpret_cast<LPRELEASEEFFECTDATA>(GetProcAddress(hDll, "ReleaseEffectData"));

    int effectsCount = 1;
    if (GetEffectsCount)
    {
        const int pluginEffectsCount = GetEffectsCount();
        if (pluginEffectsCount > 0)
            effectsCount = pluginEffectsCount;
    }

    if (effectsCount > 1)
    {
        sideBySide = true;
        std::cout << "Plugin effects count: " << effectsCount
            << ". Auto-enabled side-by-side mode.\n";
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    unsigned char* inputPixels = stbi_load(
        inputPng,
        &width,
        &height,
        &channels,
        4
    );

    if (!inputPixels)
    {
        std::cerr << "Failed to load input image: " << inputPng << "\n";
        FreeLibrary(hDll);
        return 1;
    }

    if (width <= 0 || height <= 0)
    {
        std::cerr << "Invalid image size.\n";
        stbi_image_free(inputPixels);
        FreeLibrary(hDll);
        return 1;
    }

    const int frameCount = std::max(1, static_cast<int>(std::round(fps * durationSeconds)));
    const int outputFrameCount = GetOutputFrameCount(frameCount, pingPong);
    const int delayCs = std::max(1, 100 / fps);    // GIF delay in hundredths of a second
    const size_t imageSize = static_cast<size_t>(width) * height * 4;

    GifWriter writer;

    if (!GifBegin(&writer, outputGif, width, height, delayCs))
    {
        std::cerr << "Failed to create GIF: " << outputGif << "\n";
        stbi_image_free(inputPixels);
        FreeLibrary(hDll);
        return 1;
    }

    // Plugin-owned temporary state. The plugin may allocate/update it on one call
    // and receive the same pointer again on the next ApplyEffect call.
    void* effectData = nullptr;
    auto releaseEffectData = [&]()
    {
        if (effectData && ReleaseEffectData)
            ReleaseEffectData(effectData);

        effectData = nullptr;
    };

    if (sideBySide)
    {
        std::vector<BYTE> frame(imageSize);
        const int totalSideBySideFrames = outputFrameCount * effectsCount;

        for (int effectId = 0; effectId < effectsCount; ++effectId)
        {
            releaseEffectData();

            std::string effectName;
            if (GetEffectName)
            {
                const wchar_t* pluginEffectName = GetEffectName(effectId);
                effectName = WideStringToUtf8(pluginEffectName);

                if (pluginEffectName && ReleasePluginString)
                    ReleasePluginString(pluginEffectName);
            }

            std::cout << "Effect " << (effectId + 1) << "/" << effectsCount;
            if (!effectName.empty())
                std::cout << ": " << effectName;
            std::cout << "\n";

            std::vector<BYTE> after(inputPixels, inputPixels + imageSize);
            const double pluginCompleteness = forceCompleteness ? forcedCompleteness : 1.0;
            HRESULT hr = ApplyEffect(
                after.data(),
                width,
                height,
                pluginCompleteness,
                effectId,
                nullptr,
                &effectData
            );

            if (FAILED(hr))
            {
                std::cerr << "ApplyEffect failed for side-by-side effect " << effectId
                    << ", HRESULT = 0x" << std::hex << hr << std::dec << "\n";

                GifEnd(&writer);
                releaseEffectData();
                stbi_image_free(inputPixels);
                FreeLibrary(hDll);
                return 1;
            }

            releaseEffectData();

            for (int i = 0; i < outputFrameCount; ++i)
            {
                const int sourceFrame = GetSourceFrameIndex(i, frameCount, pingPong);
                double t = 0.0;

                if (frameCount > 1)
                    t = static_cast<double>(sourceFrame) / static_cast<double>(frameCount - 1);

                const int dividerX = static_cast<int>(t * width + 0.5);

                BuildSideBySideFrame(
                    frame.data(),
                    after.data(),
                    inputPixels,
                    width,
                    height,
                    dividerX
                );

                GifWriteFrame(
                    &writer,
                    frame.data(),
                    width,
                    height,
                    delayCs
                );

                const int globalFrame = effectId * outputFrameCount + i + 1;
                std::cout << "Frame " << globalFrame << "/" << totalSideBySideFrames
                    << ", effect = " << effectId
                    << ", divider = " << dividerX << "\n";
            }
        }
    }
    else
    {
        std::vector<BYTE> frame(imageSize);

        for (int i = 0; i < outputFrameCount; ++i)
        {
            const int sourceFrame = GetSourceFrameIndex(i, frameCount, pingPong);
            double t = 0.0;

            if (frameCount > 1)
                t = static_cast<double>(sourceFrame) / static_cast<double>(frameCount - 1);

            memcpy(frame.data(), inputPixels, imageSize);
            const double pluginCompleteness = forceCompleteness ? forcedCompleteness : t;

            HRESULT hr = ApplyEffect(
                frame.data(),
                width,
                height,
                pluginCompleteness,
                0,
                nullptr,
                &effectData
            );

            if (FAILED(hr))
            {
                std::cerr << "ApplyEffect failed at frame " << i << ", HRESULT = 0x"
                    << std::hex << hr << std::dec << "\n";

                GifEnd(&writer);
                releaseEffectData();
                stbi_image_free(inputPixels);
                FreeLibrary(hDll);
                return 1;
            }

            GifWriteFrame(
                &writer,
                frame.data(),
                width,
                height,
                delayCs
            );

            std::cout << "Frame " << (i + 1) << "/" << outputFrameCount
                << ", t = " << t << "\n";
        }
    }

    GifEnd(&writer);

    releaseEffectData();
    stbi_image_free(inputPixels);
    FreeLibrary(hDll);

    std::cout << "Done: " << outputGif << "\n";
    return 0;
}

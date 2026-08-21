#include "pch.h"
#include "plugin.h"
#include "helper.h"
#include <windows.h>
#include <stdexcept>

static std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
bool GenerateSpeech(SupertonicPlugin& plugin, const GenerationOptions& options, std::wstring& error) {
    try {
        if (plugin.modelDirectory.empty()) throw std::runtime_error("Supertonic model directory was not found");
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "AVS-Supertonic3");
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const auto onnx = Utf8((plugin.modelDirectory / L"onnx").wstring());
        auto tts = loadTextToSpeech(env, onnx, false);
        const auto stylePath = Utf8(options.voiceStyle.wstring());
        auto style = loadVoiceStyle({stylePath}, false);
        auto result = tts->call(memory, Utf8(options.text), Utf8(options.language), style,
                                options.steps, options.speed);
        const int count = (std::min)(static_cast<int>(result.wav.size()),
            static_cast<int>(tts->getSampleRate() * result.duration.front()));
        result.wav.resize(count);
        writeWavFile(Utf8(plugin.lastOutput.wstring()), result.wav, tts->getSampleRate());
        clearTensorBuffers();
        return true;
    } catch (const std::exception& e) {
        const std::string what = e.what();
        const int size = MultiByteToWideChar(CP_UTF8, 0, what.data(), static_cast<int>(what.size()), nullptr, 0);
        error.resize(size);
        if (size > 0) MultiByteToWideChar(CP_UTF8, 0, what.data(), static_cast<int>(what.size()), error.data(), size);
        return false;
    }
}




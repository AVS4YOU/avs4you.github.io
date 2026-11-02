#pragma once
// Vocals in another voice, with no UI: what the plugin window drives. ffmpeg
// decodes the vocals and the voice sample, seedvc.exe (Seed-VC on ONNX
// Runtime, GPL-3.0, a separate program) re-voices them, ffmpeg writes the result.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct VoiceModelFile {
	const wchar_t* name;   // in workDirectory\models
	std::uint64_t size;
};
const std::vector<VoiceModelFile>& VoiceModelFiles();

struct VoiceQuality {
	const wchar_t* id;     // what settings store
	const wchar_t* label;  // English; the UI translates it
	int steps;             // diffusion steps
};
const std::vector<VoiceQuality>& VoiceQualities();
const VoiceQuality* FindVoiceQuality(const std::wstring& id);

struct VoiceFormat {
	const wchar_t* id;
	const wchar_t* label;
	const wchar_t* extension;
	const wchar_t* encoder;  // ffmpeg arguments
};
const std::vector<VoiceFormat>& VoiceFormats();
const VoiceFormat* FindVoiceFormat(const std::wstring& id);

struct VoiceJob {
	std::filesystem::path input;   // the vocals: a voice without music
	std::filesystem::path voice;   // a sample of the voice to sing with
	std::filesystem::path output;
	std::wstring quality = L"standard";
	int pitch = 0;                 // semitones
	bool matchVoicePitch = false;  // move the melody to the voice's own range
	bool useGpu = true;
};

struct VoiceTools {
	std::filesystem::path toolsDirectory;  // ffmpeg.exe, seedvc\seedvc.exe
	std::filesystem::path workDirectory;   // models\ and tmp\
	// status texts are English keys; the plugin translates
	std::function<std::wstring(const wchar_t*)> translate = [](const wchar_t* text) {
		return std::wstring(text);
	};
};

using VoiceProgress = std::function<void(const std::wstring& text, int percent)>;

// bytes still to download before the first conversion; 0 when all is there
std::uint64_t MissingVoiceModelBytes(const VoiceTools& tools);

// "<name> - new voice.<ext>" next to input
std::filesystem::path DefaultVoiceOutput(const std::filesystem::path& input, const VoiceFormat& format);

bool ConvertVoice(const VoiceTools& tools, const VoiceJob& job, const VoiceFormat& format,
	const VoiceProgress& progress, const std::atomic_bool& cancel, std::wstring& error);

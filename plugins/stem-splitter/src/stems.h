#pragma once
// Music to separate tracks, with no UI: what the plugin window and a test
// harness both drive. ffmpeg decodes any audio or video, stems.exe (Demucs v4
// on ONNX Runtime) separates it, and ffmpeg writes each track.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct StemModel {
	const wchar_t* id;     // what settings store
	const wchar_t* label;  // English; the UI translates it
	const wchar_t* file;   // the ONNX model, downloaded on first use
	std::uint64_t size;
	bool vocalsOnly;       // vocals + the mix without them
	std::vector<const wchar_t*> stems;  // English track names, in stems.exe's order
};
const std::vector<StemModel>& StemModels();
const StemModel* FindStemModel(const std::wstring& id);

struct StemFormat {
	const wchar_t* id;
	const wchar_t* label;
	const wchar_t* extension;
	const wchar_t* encoder;  // ffmpeg arguments
};
const std::vector<StemFormat>& StemFormats();
const StemFormat* FindStemFormat(const std::wstring& id);

struct StemJob {
	std::filesystem::path input;
	std::filesystem::path outputDirectory;
	std::wstring model = L"vocals";
	std::wstring format = L"wav";
};

struct StemTools {
	std::filesystem::path toolsDirectory;  // ffmpeg.exe, stems.exe, onnxruntime.dll
	std::filesystem::path workDirectory;   // models/ and tmp/
	// where a run keeps its raw audio - about 21 MB a minute per track - in
	// a "avs-stems-<n>" subfolder; empty: workDirectory\tmp
	std::filesystem::path tempDirectory;
	// status texts and track names are English keys; the plugin translates
	std::function<std::wstring(const wchar_t*)> translate = [](const wchar_t* text) {
		return std::wstring(text);
	};
};

using StemProgress = std::function<void(const std::wstring& text, int percent)>;

bool IsStemModelPresent(const StemTools& tools, const StemModel& model);

// the scratch folders a run makes in a chosen temporary folder, and that one
// which did not finish leaves behind
constexpr wchar_t STEM_SCRATCH_PREFIX[] = L"avs-stems-";
void RemoveStemScratch(const std::filesystem::path& tempDirectory);

// "<name> - stems" next to input
std::filesystem::path DefaultStemFolder(const std::filesystem::path& input);

// The files a job writes: "<name> - <Track>.<ext>" in its output directory.
std::vector<std::filesystem::path> StemOutputs(const StemTools& tools, const StemJob& job);

bool SeparateStems(const StemTools& tools, const StemJob& job, const StemProgress& progress,
	const std::atomic_bool& cancel, std::wstring& error);

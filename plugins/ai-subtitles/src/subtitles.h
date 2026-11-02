#pragma once
// Speech to subtitles, with no UI: what the plugin window and a test harness
// both drive. ffmpeg pulls the audio out of any video or audio file,
// whisper-cli recognizes the speech Silero VAD finds in it, and each cue is
// timed by its words' DTW times rather than Whisper's own whole-second guesses.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct WhisperModel {
	const wchar_t* id;     // what settings store
	const wchar_t* label;  // English; the UI translates it
	const wchar_t* file;   // ggml file on huggingface.co/ggerganov/whisper.cpp
	std::uint64_t size;
	const wchar_t* dtw;    // whisper-cli's -dtw preset: the model's alignment heads
};
const std::vector<WhisperModel>& WhisperModels();
const WhisperModel* FindWhisperModel(const std::wstring& id);

struct SubtitleLanguage {
	const wchar_t* code;  // whisper's; "auto" detects
	const wchar_t* name;  // in that language, so it needs no translation
};
const std::vector<SubtitleLanguage>& SubtitleLanguages();

struct SubtitleJob {
	std::filesystem::path input;
	std::filesystem::path output;  // the .srt to write
	std::wstring model = L"small";
	std::wstring language = L"auto";
	bool translate = false;        // into English
	// Silero VAD: Whisper hears only the speech it finds, which keeps cues off
	// music and silence. Off for songs - VAD does not take singing for speech.
	bool speechOnly = true;
	bool useGpu = true;  // the Vulkan build, where the package has it (x64)
};

struct SubtitleTools {
	std::filesystem::path toolsDirectory;  // ffmpeg.exe, whisper-cli*.exe
	std::filesystem::path workDirectory;   // models/ and tmp/
	// status texts are English keys; the plugin passes its translation
	std::function<std::wstring(const wchar_t*)> translate = [](const wchar_t* text) {
		return std::wstring(text);
	};
};

// text for the status line; percent 0-100, or -1 to leave the bar alone
using SubtitleProgress = std::function<void(const std::wstring& text, int percent)>;

bool IsWhisperModelPresent(const SubtitleTools& tools, const WhisperModel& model);

// whether this package has the Vulkan build - the x86 one does not
bool HasGpuEngine(const SubtitleTools& tools);

// <name>.srt next to input
std::filesystem::path DefaultSubtitlePath(const std::filesystem::path& input);

bool MakeSubtitles(const SubtitleTools& tools, const SubtitleJob& job,
	const SubtitleProgress& progress, const std::atomic_bool& cancel, std::wstring& error);

// Exposed for tests: whisper's .srt, tidied - trimmed, wrapped into at most
// two balanced lines, empty cues dropped, renumbered, CRLF.
std::string TidySrt(const std::string& srt);

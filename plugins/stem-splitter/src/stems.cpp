#include "stems.h"
#include "../../../sdk/common/ChildProcess.h"
#include "../../../sdk/common/HttpDownload.h"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <thread>

namespace {
	// tools/export_demucs_onnx.py makes these from Meta's Demucs v4 weights (MIT);
	// tools/publish_assets.py publishes them under this tag
	const wchar_t MODEL_URL[] =
		L"https://github.com/AVS4YOU/avs4you.github.io/releases/download/models-demucs-onnx-1/";

	std::wstring Widen(const std::string& text) {
		if (text.empty())
			return {};
		const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
		std::wstring result(length, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), length);
		return result;
	}

	double ParseClock(const std::string& text) {
		int hours = 0, minutes = 0;
		double seconds = 0;
		if (sscanf_s(text.c_str(), "%d:%d:%lf", &hours, &minutes, &seconds) != 3)
			return 0;
		return hours * 3600.0 + minutes * 60.0 + seconds;
	}

	std::wstring Megabytes(std::uint64_t bytes) {
		return std::to_wstring((bytes + 512 * 1024) / (1024 * 1024)) + L" MB";
	}

	std::wstring LastLines(const std::vector<std::string>& lines, size_t count) {
		std::wstring result;
		for (size_t index = lines.size() > count ? lines.size() - count : 0; index < lines.size(); ++index)
			result += L"\r\n" + Widen(lines[index]);
		return result;
	}

	std::filesystem::path ModelPath(const StemTools& tools, const StemModel& model) {
		return tools.workDirectory / L"models" / model.file;
	}

	// stems.exe is always 64-bit: Demucs's attention needs blocks of a few
	// hundred MB that a 32-bit address space cannot give. The x86 plugin
	// therefore runs it too, which takes 64-bit Windows.
	bool Is64BitWindows() {
#if defined(_WIN64)
		return true;
#else
		BOOL wow64 = FALSE;
		return IsWow64Process(GetCurrentProcess(), &wow64) && wow64;
#endif
	}

	// every logical processor: on 6 cores / 12 threads 12 threads were 12%
	// faster than 6. stems.exe splits them between at most 3 jobs at once
	unsigned Threads() {
		return (std::min)((std::max)(1u, std::thread::hardware_concurrency()), 64u);
	}

	// what ffmpeg writes to stderr when a file has no audio
	bool NoAudio(const std::vector<std::string>& output) {
		for (const auto& line : output)
			if (line.find("does not contain any stream") != std::string::npos ||
				line.find("matches no streams") != std::string::npos)
				return true;
		return false;
	}
} // namespace

const std::vector<StemModel>& StemModels() {
	static const std::vector<StemModel> models = {
		{L"vocals", L"Vocals and instrumental", L"htdemucs_ft_vocals.onnx", 174263526, true,
			{L"Vocals", L"Instrumental"}},
		{L"4stems", L"4 tracks: vocals, drums, bass, other", L"htdemucs.onnx", 174263526, false,
			{L"Drums", L"Bass", L"Other", L"Vocals"}},
		{L"6stems", L"6 tracks: plus guitar and piano", L"htdemucs_6s.onnx", 114559139, false,
			{L"Drums", L"Bass", L"Other", L"Vocals", L"Guitar", L"Piano"}},
	};
	return models;
}

const StemModel* FindStemModel(const std::wstring& id) {
	for (const auto& model : StemModels())
		if (id == model.id)
			return &model;
	return nullptr;
}

const std::vector<StemFormat>& StemFormats() {
	static const std::vector<StemFormat> formats = {
		{L"wav", L"WAV (16-bit)", L".wav", L"-c:a pcm_s16le"},
		{L"flac", L"FLAC (lossless)", L".flac", L"-c:a flac"},
		{L"mp3", L"MP3 (320 kbps)", L".mp3", L"-c:a libmp3lame -b:a 320k"},
	};
	return formats;
}

const StemFormat* FindStemFormat(const std::wstring& id) {
	for (const auto& format : StemFormats())
		if (id == format.id)
			return &format;
	return nullptr;
}

bool IsStemModelPresent(const StemTools& tools, const StemModel& model) {
	std::error_code error;
	return std::filesystem::file_size(ModelPath(tools, model), error) == model.size && !error;
}

void RemoveStemScratch(const std::filesystem::path& tempDirectory) {
	std::error_code error;
	for (std::filesystem::directory_iterator entry(tempDirectory, error), end; !error && entry != end;
		entry.increment(error)) {
		std::error_code ignored;
		if (entry->is_directory(ignored) &&
			entry->path().filename().wstring().rfind(STEM_SCRATCH_PREFIX, 0) == 0)
			std::filesystem::remove_all(entry->path(), ignored);
	}
}

std::filesystem::path DefaultStemFolder(const std::filesystem::path& input) {
	return input.parent_path() / (input.stem().wstring() + L" - stems");
}

std::vector<std::filesystem::path> StemOutputs(const StemTools& tools, const StemJob& job) {
	std::vector<std::filesystem::path> outputs;
	const StemModel* model = FindStemModel(job.model);
	const StemFormat* format = FindStemFormat(job.format);
	if (!model || !format)
		return outputs;
	for (const wchar_t* stem : model->stems)
		outputs.push_back(job.outputDirectory /
			(job.input.stem().wstring() + L" - " + tools.translate(stem) + format->extension));
	return outputs;
}

bool SeparateStems(const StemTools& tools, const StemJob& job, const StemProgress& progress,
	const std::atomic_bool& cancel, std::wstring& error) {
	const auto& tr = tools.translate;
	const StemModel* model = FindStemModel(job.model);
	const StemFormat* format = FindStemFormat(job.format);
	if (!model || !format) {
		error = L"Unknown model or format";
		return false;
	}
	if (!Is64BitWindows()) {
		error = tr(L"Stem Splitter needs 64-bit Windows.");
		return false;
	}
	const auto ffmpegExe = NSChildProcess::Quote((tools.toolsDirectory / L"ffmpeg.exe").wstring());

	// 1. the model, downloaded once
	if (!IsStemModelPresent(tools, *model)) {
		const std::wstring what = tr(L"Downloading separation model") + L" (" + Megabytes(model->size) + L")";
		progress(what, 0);
		int lastPercent = -1;
		if (!NSHttpDownload::Download(MODEL_URL + std::wstring(model->file), ModelPath(tools, *model),
			model->size,
			[&](std::uint64_t received, std::uint64_t total) {
				const int percent = total ? static_cast<int>(received * 100 / total) : 0;
				if (percent != lastPercent) {
					lastPercent = percent;
					progress(what + L": " + Megabytes(received), percent);
				}
			}, cancel, error))
			return false;
	}

	// 2. a scratch folder: under the work directory, or in the chosen one
	const auto stamp = std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
	const auto scratch = tools.tempDirectory.empty() ? tools.workDirectory / L"tmp" / stamp
		: tools.tempDirectory / (STEM_SCRATCH_PREFIX + stamp);
	std::error_code ignored;
	std::filesystem::create_directories(scratch, ignored);
	if (!std::filesystem::is_directory(scratch, ignored)) {
		error = tr(L"Cannot create a temporary folder in") + L" " + scratch.parent_path().wstring();
		return false;
	}
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code ignored;
			std::filesystem::remove_all(path, ignored);
		}
	} cleanup{ scratch };

	// 3. the audio as raw 44.1 kHz stereo float, which is what Demucs takes
	const std::wstring decoding = tr(L"Reading audio...");
	progress(decoding, 0);
	double duration = 0;
	std::vector<std::string> ffmpegOutput;
	const int decodeExit = NSChildProcess::Run(ffmpegExe +
		L" -hide_banner -nostdin -y -i " + NSChildProcess::Quote(job.input.wstring()) +
		L" -vn -ac 2 -ar 44100 -f f32le -c:a pcm_f32le -progress pipe:1 -nostats input.f32",
		scratch.wstring(),
		[&](const std::string& line) {
			ffmpegOutput.push_back(line);
			const size_t at = line.find("Duration: ");
			if (at != std::string::npos && duration == 0)
				duration = ParseClock(line.substr(at + 10));
			if (line.rfind("out_time_us=", 0) == 0 && duration > 0)
				progress(decoding, std::clamp(static_cast<int>(std::atof(line.c_str() + 12) / 1e6 * 5 / duration), 0, 5));
		}, cancel, error);
	if (decodeExit != 0) {
		if (error.empty())
			error = NoAudio(ffmpegOutput) ? tr(L"The file has no audio track.")
			: tr(L"Cannot read the audio of this file.") + LastLines(ffmpegOutput, 3);
		return false;
	}

	// 4. separation
	const std::wstring separating = tr(L"Separating tracks...");
	progress(separating, 5);
	std::vector<std::string> stemsOutput;
	std::wstring stems = NSChildProcess::Quote((tools.toolsDirectory / L"stems.exe").wstring()) +
		L" " + NSChildProcess::Quote(ModelPath(tools, *model).wstring()) +
		L" input.f32 . " + std::to_wstring(Threads());
	if (model->vocalsOnly)
		stems += L" vocals";
	const int stemsExit = NSChildProcess::Run(stems, scratch.wstring(),
		[&](const std::string& line) {
			stemsOutput.push_back(line);
			if (line.rfind("progress ", 0) == 0)
				progress(separating + L" " + Widen(line.substr(9)) + L"%",
					5 + std::clamp(std::atoi(line.c_str() + 9), 0, 100) * 85 / 100);
		}, cancel, error);
	if (stemsExit != 0) {
		if (error.empty())
			error = tr(L"Separation failed.") + LastLines(stemsOutput, 3);
		return false;
	}

	// 5. every track in the chosen format, into the output folder
	std::filesystem::create_directories(job.outputDirectory, ignored);
	const auto outputs = StemOutputs(tools, job);
	// stems.exe writes lowercase English names in the model's order
	for (size_t index = 0; index < outputs.size(); ++index) {
		std::wstring raw = model->stems[index];
		std::transform(raw.begin(), raw.end(), raw.begin(), ::towlower);
		progress(tr(L"Saving") + L" " + outputs[index].filename().wstring(),
			90 + static_cast<int>(index * 10 / outputs.size()));
		std::vector<std::string> encodeOutput;
		const int encodeExit = NSChildProcess::Run(ffmpegExe +
			L" -hide_banner -nostdin -y -f f32le -ar 44100 -ac 2 -i " + raw + L".f32 " + format->encoder +
			L" " + NSChildProcess::Quote(outputs[index].wstring()),
			scratch.wstring(),
			[&](const std::string& line) { encodeOutput.push_back(line); }, cancel, error);
		if (encodeExit != 0) {
			if (error.empty())
				error = tr(L"Cannot write") + L" " + outputs[index].wstring() + LastLines(encodeOutput, 2);
			return false;
		}
	}
	progress(tr(L"Tracks are ready"), 100);
	return true;
}

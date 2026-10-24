#include "voice.h"
#include "../../../sdk/common/ChildProcess.h"
#include "../../../sdk/common/HttpDownload.h"

#include <windows.h>
#include <algorithm>
#include <chrono>

namespace {
	// engine/export_onnx.py makes these from Seed-VC's singing model and the
	// models it builds on (Whisper, RMVPE, CAMPPlus, BigVGAN)
	const wchar_t MODEL_URL[] =
		L"https://github.com/AVS4YOU/avs4you.github.io/releases/download/models-seedvc-onnx-1/";

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

	std::filesystem::path ModelPath(const VoiceTools& tools, const VoiceModelFile& file) {
		return tools.workDirectory / L"models" / file.name;
	}

	bool IsPresent(const VoiceTools& tools, const VoiceModelFile& file) {
		std::error_code error;
		return std::filesystem::file_size(ModelPath(tools, file), error) == file.size && !error;
	}

	// seedvc.exe is 64-bit: the x86 plugin runs it too, which takes 64-bit Windows
	bool Is64BitWindows() {
#if defined(_WIN64)
		return true;
#else
		BOOL wow64 = FALSE;
		return IsWow64Process(GetCurrentProcess(), &wow64) && wow64;
#endif
	}

	bool NoAudio(const std::vector<std::string>& output) {
		for (const auto& line : output)
			if (line.find("does not contain any stream") != std::string::npos ||
				line.find("matches no streams") != std::string::npos)
				return true;
		return false;
	}

	// a file's first audio track as raw mono float at 44.1 and at 16 kHz
	bool Decode(const VoiceTools& tools, const std::filesystem::path& input, const std::wstring& name,
		const std::filesystem::path& scratch, const VoiceProgress& progress, const std::wstring& what,
		int from, int to, const std::atomic_bool& cancel, std::wstring& error) {
		const auto& tr = tools.translate;
		double duration = 0;
		std::vector<std::string> output;
		const int exitCode = NSChildProcess::Run(
			NSChildProcess::Quote((tools.toolsDirectory / L"ffmpeg.exe").wstring()) +
			L" -hide_banner -nostdin -y -i " + NSChildProcess::Quote(input.wstring()) +
			L" -map 0:a:0 -vn -ac 1 -ar 44100 -f f32le -c:a pcm_f32le " + name + L".f32" +
			L" -map 0:a:0 -vn -ac 1 -ar 16000 -f f32le -c:a pcm_f32le " + name + L"16.f32" +
			L" -progress pipe:1 -nostats",
			scratch.wstring(),
			[&](const std::string& line) {
				output.push_back(line);
				const size_t at = line.find("Duration: ");
				if (at != std::string::npos && duration == 0)
					duration = ParseClock(line.substr(at + 10));
				if (line.rfind("out_time_us=", 0) == 0 && duration > 0)
					progress(what, from + std::clamp(static_cast<int>(
						std::atof(line.c_str() + 12) / 1e6 * (to - from) / duration), 0, to - from));
			}, cancel, error);
		if (exitCode != 0) {
			if (error.empty())
				error = NoAudio(output) ? tr(L"The file has no audio track:") + L" " + input.filename().wstring()
				: tr(L"Cannot read the audio of this file:") + L" " + input.filename().wstring() + LastLines(output, 3);
			return false;
		}
		return true;
	}
} // namespace

const std::vector<VoiceModelFile>& VoiceModelFiles() {
	static const std::vector<VoiceModelFile> files = {
		{L"filters.npz", 852530},
		{L"whisper_encoder.onnx", 352712079},
		{L"campplus.onnx", 28690673},
		{L"rmvpe.onnx", 361688039},
		{L"length_regulator.onnx", 33868204},
		{L"dit_fp16.onnx", 351710509},
		{L"bigvgan.onnx", 490977308},
	};
	return files;
}

const std::vector<VoiceQuality>& VoiceQualities() {
	static const std::vector<VoiceQuality> qualities = {
		{L"fast", L"Fast", 12},
		{L"standard", L"Standard", 25},
		{L"best", L"Best (slow)", 40},
	};
	return qualities;
}

const VoiceQuality* FindVoiceQuality(const std::wstring& id) {
	for (const auto& quality : VoiceQualities())
		if (id == quality.id)
			return &quality;
	return nullptr;
}

const std::vector<VoiceFormat>& VoiceFormats() {
	static const std::vector<VoiceFormat> formats = {
		{L"wav", L"WAV (16-bit)", L".wav", L"-c:a pcm_s16le"},
		{L"flac", L"FLAC (lossless)", L".flac", L"-c:a flac"},
		{L"mp3", L"MP3 (320 kbps)", L".mp3", L"-c:a libmp3lame -b:a 320k"},
	};
	return formats;
}

const VoiceFormat* FindVoiceFormat(const std::wstring& id) {
	for (const auto& format : VoiceFormats())
		if (id == format.id)
			return &format;
	return nullptr;
}

std::uint64_t MissingVoiceModelBytes(const VoiceTools& tools) {
	std::uint64_t missing = 0;
	for (const auto& file : VoiceModelFiles())
		if (!IsPresent(tools, file))
			missing += file.size;
	return missing;
}

std::filesystem::path DefaultVoiceOutput(const std::filesystem::path& input, const VoiceFormat& format) {
	return input.parent_path() / (input.stem().wstring() + L" - new voice" + format.extension);
}

bool ConvertVoice(const VoiceTools& tools, const VoiceJob& job, const VoiceFormat& format,
	const VoiceProgress& progress, const std::atomic_bool& cancel, std::wstring& error) {
	const auto& tr = tools.translate;
	const VoiceQuality* quality = FindVoiceQuality(job.quality);
	if (!quality)
		quality = FindVoiceQuality(L"standard");
	if (!Is64BitWindows()) {
		error = tr(L"Voice Clone needs 64-bit Windows.");
		return false;
	}

	// 1. the models, downloaded once
	const std::uint64_t missing = MissingVoiceModelBytes(tools);
	if (missing) {
		const std::wstring what = tr(L"Downloading the voice model") + L" (" + Megabytes(missing) + L")";
		std::uint64_t done = 0;
		for (const auto& file : VoiceModelFiles()) {
			if (IsPresent(tools, file))
				continue;
			int lastPercent = -1;
			if (!NSHttpDownload::Download(MODEL_URL + std::wstring(file.name), ModelPath(tools, file), file.size,
				[&](std::uint64_t received, std::uint64_t) {
					const int percent = static_cast<int>((done + received) * 100 / missing);
					if (percent != lastPercent) {
						lastPercent = percent;
						progress(what + L": " + Megabytes(done + received), percent);
					}
				}, cancel, error))
				return false;
			done += file.size;
		}
	}

	// 2. a scratch folder
	const auto stamp = std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
	const auto scratch = tools.workDirectory / L"tmp" / stamp;
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

	// 3. both recordings as raw audio
	const std::wstring reading = tr(L"Reading audio...");
	progress(reading, 0);
	if (!Decode(tools, job.voice, L"voice", scratch, progress, reading, 0, 1, cancel, error) ||
		!Decode(tools, job.input, L"song", scratch, progress, reading, 1, 3, cancel, error))
		return false;

	// 4. the new voice
	const std::wstring converting = tr(L"Changing the voice...");
	progress(converting, 3);
	std::vector<std::string> engineOutput;
	std::wstring command = NSChildProcess::Quote((tools.toolsDirectory / L"seedvc" / L"seedvc.exe").wstring()) +
		L" --models " + NSChildProcess::Quote((tools.workDirectory / L"models").wstring()) +
		L" --source song.f32 --source16k song16.f32 --reference voice.f32 --reference16k voice16.f32" +
		L" --output output.f32 --steps " + std::to_wstring(quality->steps) +
		L" --pitch " + std::to_wstring(job.pitch) + L" --device " + (job.useGpu ? L"auto" : L"cpu");
	if (job.matchVoicePitch)
		command += L" --auto-pitch";
	std::wstring device;
	const int engineExit = NSChildProcess::Run(command, scratch.wstring(),
		[&](const std::string& line) {
			engineOutput.push_back(line);
			if (line.rfind("device ", 0) == 0)
				device = Widen(line.substr(7)) == L"gpu" ? tr(L"graphics card") : tr(L"processor");
			else if (line.rfind("gpu failed", 0) == 0)
				device = tr(L"processor");
			else if (line.rfind("progress ", 0) == 0) {
				const int percent = std::clamp(std::atoi(line.c_str() + 9), 0, 100);
				progress(converting + L" " + std::to_wstring(percent) + L"%" +
					(device.empty() ? L"" : L" (" + device + L")"), 3 + percent * 94 / 100);
			}
		}, cancel, error);
	if (engineExit != 0) {
		if (error.empty()) {
			std::wstring message;
			for (const auto& line : engineOutput)
				if (line.rfind("error: ", 0) == 0)
					message = Widen(line.substr(7));
			error = tr(L"Changing the voice failed.") +
				(message.empty() ? LastLines(engineOutput, 3) : L"\r\n" + message);
		}
		return false;
	}

	// 5. the result in the chosen format
	progress(tr(L"Saving") + L" " + job.output.filename().wstring(), 97);
	std::filesystem::create_directories(job.output.parent_path(), ignored);
	std::vector<std::string> encodeOutput;
	const int encodeExit = NSChildProcess::Run(
		NSChildProcess::Quote((tools.toolsDirectory / L"ffmpeg.exe").wstring()) +
		L" -hide_banner -nostdin -y -f f32le -ar 44100 -ac 1 -i output.f32 " + format.encoder + L" " +
		NSChildProcess::Quote(job.output.wstring()),
		scratch.wstring(),
		[&](const std::string& line) { encodeOutput.push_back(line); }, cancel, error);
	if (encodeExit != 0) {
		if (error.empty())
			error = tr(L"Cannot write") + L" " + job.output.wstring() + LastLines(encodeOutput, 2);
		return false;
	}
	progress(tr(L"The new voice is ready"), 100);
	return true;
}

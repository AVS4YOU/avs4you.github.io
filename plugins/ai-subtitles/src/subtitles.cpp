#include "subtitles.h"
#include "../../../sdk/common/ChildProcess.h"
#include "../../../sdk/common/CpuFeatures.h"
#include "../../../sdk/common/HttpDownload.h"
#include "../../../sdk/3dParty/nlohmann/json/single_include/nlohmann/json.hpp"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <thread>

namespace {
	const wchar_t MODEL_URL[] = L"https://huggingface.co/ggerganov/whisper.cpp/resolve/main/";

	// a subtitle line: 42 characters, two lines a cue - the usual broadcast limits
	constexpr size_t LINE_LENGTH = 42;

	std::wstring Widen(const std::string& text) {
		if (text.empty())
			return {};
		const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
		std::wstring result(length, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), length);
		return result;
	}

	std::string Narrow(const std::wstring& text) {
		if (text.empty())
			return {};
		const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0,
			nullptr, nullptr);
		std::string result(length, '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), length,
			nullptr, nullptr);
		return result;
	}

	std::wstring Trim(const std::wstring& text) {
		const size_t first = text.find_first_not_of(L" \t\r\n");
		if (first == std::wstring::npos)
			return {};
		return text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
	}

	// "01:02:03.45" -> seconds
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

	// one cue's text in at most two lines, split at the space nearest the middle
	std::wstring Wrap(const std::wstring& text) {
		if (text.size() <= LINE_LENGTH)
			return text;
		const size_t middle = text.size() / 2;
		size_t best = std::wstring::npos;
		for (size_t index = 0; index < text.size(); ++index)
			if (text[index] == L' ' &&
				(best == std::wstring::npos ||
					(std::max)(index, middle) - (std::min)(index, middle) <
					(std::max)(best, middle) - (std::min)(best, middle)))
				best = index;
		if (best == std::wstring::npos)  // no spaces: Chinese, Japanese...
			return text.substr(0, middle) + L"\r\n" + text.substr(middle);
		return text.substr(0, best) + L"\r\n" + text.substr(best + 1);
	}

	std::filesystem::path ModelPath(const SubtitleTools& tools, const WhisperModel& model) {
		return tools.workDirectory / L"models" / model.file;
	}

	// Silero VAD for whisper.cpp: Whisper hears only the speech it finds. Without
	// it, speech that starts more than a second into the audio gets its first
	// cues pinned to 0:00 and the rest of that 30 s window early with them.
	const wchar_t VAD_URL[] = L"https://huggingface.co/ggml-org/whisper-vad/resolve/main/";
	const wchar_t VAD_FILE[] = L"ggml-silero-v5.1.2.bin";
	constexpr std::uint64_t VAD_SIZE = 885098;

	// Where a stretch of speech sits in the original audio and in the
	// speech-only audio whisper-cli recognizes, in seconds. Segment times it maps
	// back itself, token times it does not; it logs the table:
	// "vad_segment_info: orig_start: 2.50, orig_end: 7.71, vad_start: 0.00, vad_end: 5.21"
	struct SpeechSpan {
		double origStart, origEnd, vadStart, vadEnd;
	};

	std::vector<SpeechSpan> SpeechSpans(const std::vector<std::string>& log) {
		std::vector<SpeechSpan> spans;
		for (const auto& line : log) {
			const size_t at = line.find("vad_segment_info:");
			SpeechSpan span{};
			if (at != std::string::npos && sscanf_s(line.c_str() + at,
				"vad_segment_info: orig_start: %lf, orig_end: %lf, vad_start: %lf, vad_end: %lf",
				&span.origStart, &span.origEnd, &span.vadStart, &span.vadEnd) == 4)
				spans.push_back(span);
		}
		return spans;
	}

	// a speech-only time in the original audio, and where that speech ends;
	// the pause before a span belongs to it
	double ToOriginal(const std::vector<SpeechSpan>& spans, double time, double& speechEnd) {
		for (const auto& span : spans)
			if (time <= span.vadEnd) {
				speechEnd = span.origEnd;
				return time >= span.vadStart ? span.origStart + (time - span.vadStart) : span.origStart;
			}
		speechEnd = spans.back().origEnd;
		return spans.back().origEnd;
	}

	std::string SrtClock(double seconds) {
		const long long ms = (std::max)(0LL, std::llround(seconds * 1000));
		char text[32];
		sprintf_s(text, "%02lld:%02lld:%02lld,%03lld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60,
			ms % 1000);
		return text;
	}

	// whisper-cli's -ojf output as .srt, each cue from its first word's DTW time
	// to shortly after its last word: within the speech, never into the next
	// cue, no longer than reading it takes. With VAD, spans maps the word times
	// back. False if the JSON does not parse.
	bool SrtFromJson(std::string json, bool vad, const std::vector<SpeechSpan>& spans, std::string& srt) {
		// it writes token texts byte by byte - a token may end inside a UTF-8
		// character - and leaves control characters unescaped
		json = Narrow(Widen(json));
		for (char& c : json)
			if (static_cast<unsigned char>(c) < 0x20)
				c = ' ';
		const auto parsed = nlohmann::json::parse(json, nullptr, false);
		if (parsed.is_discarded() || !parsed.contains("transcription"))
			return false;

		struct Cue {
			double start, end;
			std::string text;
		};
		std::vector<Cue> cues;
		for (const auto& segment : parsed["transcription"]) {
			Cue cue{ segment["offsets"].value("from", 0) / 1000.0, segment["offsets"].value("to", 0) / 1000.0,
				segment.value("text", std::string()) };
			double first = 1e18, last = -1, speechEnd = 0;
			// with VAD but no table logged, the word times cannot be placed
			if ((!vad || !spans.empty()) && segment.contains("tokens"))
				for (const auto& token : segment["tokens"]) {
					const double dtw = token.value("t_dtw", -1.0);
					if (dtw < 0 || token.value("text", std::string()).rfind("[_", 0) == 0)
						continue;  // special tokens: [_BEG_], [_TT_150]...
					double end = 1e18;  // without VAD, where speech ends is unknown
					const double time = vad ? ToOriginal(spans, dtw / 100, end) : dtw / 100;
					first = (std::min)(first, time);
					if (time > last) {
						last = time;
						speechEnd = end;
					}
				}
			if (last >= 0) {
				const double reading = 1.0 + 0.1 * Trim(Widen(cue.text)).size();
				cue.start = first;
				cue.end = (std::min)({ last + 0.6, first + reading, speechEnd + 0.3 });
			}
			// with no word times: whisper-cli's own, already on the original timeline
			cues.push_back(cue);
		}
		std::sort(cues.begin(), cues.end(), [](const Cue& a, const Cue& b) { return a.start < b.start; });

		srt.clear();
		for (size_t index = 0; index < cues.size(); ++index) {
			double end = cues[index].end;
			if (index + 1 < cues.size())
				end = (std::min)(end, cues[index + 1].start - 0.04);
			end = (std::max)(end, cues[index].start + 0.5);
			srt += std::to_string(index + 1) + "\r\n" + SrtClock(cues[index].start) + " --> " +
				SrtClock(end) + "\r\n" + cues[index].text + "\r\n\r\n";
		}
		return true;
	}

	std::wstring LastLines(const std::vector<std::string>& lines, size_t count) {
		std::wstring result;
		for (size_t index = lines.size() > count ? lines.size() - count : 0; index < lines.size();
			++index)
			result += L"\r\n" + Widen(lines[index]);
		return result;
	}
} // namespace

const std::vector<WhisperModel>& WhisperModels() {
	static const std::vector<WhisperModel> models = {
		{L"base", L"Fast", L"ggml-base.bin", 147951465, L"base"},
		{L"small", L"Balanced", L"ggml-small.bin", 487601967, L"small"},
		{L"large-v3-turbo", L"Best", L"ggml-large-v3-turbo-q5_0.bin", 574041195, L"large.v3.turbo"},
	};
	return models;
}

const WhisperModel* FindWhisperModel(const std::wstring& id) {
	for (const auto& model : WhisperModels())
		if (id == model.id)
			return &model;
	return nullptr;
}

const std::vector<SubtitleLanguage>& SubtitleLanguages() {
	static const std::vector<SubtitleLanguage> languages = {
		{L"auto", L""},  // the UI shows "Detect automatically"
		{L"en", L"English"}, {L"ru", L"\x0420\x0443\x0441\x0441\x043A\x0438\x0439"},
		{L"de", L"Deutsch"}, {L"fr", L"Fran\x00E7" L"ais"}, {L"es", L"Espa\x00F1ol"},
		{L"it", L"Italiano"}, {L"pt", L"Portugu\x00EA" L"s"}, {L"nl", L"Nederlands"},
		{L"pl", L"Polski"}, {L"uk", L"\x0423\x043A\x0440\x0430\x0457\x043D\x0441\x044C\x043A\x0430"},
		{L"cs", L"\x010C" L"e\x0161tina"}, {L"da", L"Dansk"}, {L"sv", L"Svenska"},
		{L"no", L"Norsk"}, {L"fi", L"Suomi"}, {L"tr", L"T\x00FCrk\x00E7" L"e"},
		{L"el", L"\x0395\x03BB\x03BB\x03B7\x03BD\x03B9\x03BA\x03AC"},
		{L"hu", L"Magyar"}, {L"ro", L"Rom\x00E2n\x0103"},
		{L"ja", L"\x65E5\x672C\x8A9E"}, {L"ko", L"\xD55C\xAD6D\xC5B4"}, {L"zh", L"\x4E2D\x6587"},
		{L"ar", L"\x0627\x0644\x0639\x0631\x0628\x064A\x0629"},
		{L"he", L"\x05E2\x05D1\x05E8\x05D9\x05EA"},
		{L"hi", L"\x0939\x093F\x0928\x094D\x0926\x0940"},
		{L"id", L"Bahasa Indonesia"}, {L"vi", L"Ti\x1EBFng Vi\x1EC7t"},
		{L"th", L"\x0E44\x0E17\x0E22"},
	};
	return languages;
}

bool IsWhisperModelPresent(const SubtitleTools& tools, const WhisperModel& model) {
	std::error_code error;
	return std::filesystem::file_size(ModelPath(tools, model), error) == model.size && !error;
}

bool HasGpuEngine(const SubtitleTools& tools) {
	std::error_code error;
	return std::filesystem::exists(tools.toolsDirectory / L"whisper-cli-vulkan.exe", error);
}

std::filesystem::path DefaultSubtitlePath(const std::filesystem::path& input) {
	auto output = input;
	return output.replace_extension(L".srt");
}

std::string TidySrt(const std::string& srt) {
	// cues are separated by blank lines: number, timing, text lines
	std::wstring text = Widen(srt);
	text.erase(std::remove(text.begin(), text.end(), L'\r'), text.end());
	std::wistringstream input(text);
	std::wostringstream output;
	std::wstring line, timing, body;
	int number = 0;
	bool inCue = false;
	auto flush = [&] {
		const std::wstring cleaned = Trim(body);
		if (!timing.empty() && !cleaned.empty())
			output << ++number << L"\r\n" << timing << L"\r\n" << Wrap(cleaned) << L"\r\n\r\n";
		timing.clear();
		body.clear();
		inCue = false;
	};
	while (std::getline(input, line)) {
		if (Trim(line).empty()) {
			flush();
			continue;
		}
		if (line.find(L"-->") != std::wstring::npos) {
			if (inCue)
				flush();
			timing = Trim(line);
			inCue = true;
			continue;
		}
		if (!inCue)
			continue;  // the cue number: renumbered anyway
		body += (body.empty() ? L"" : L" ") + Trim(line);
	}
	flush();
	return Narrow(output.str());
}

bool MakeSubtitles(const SubtitleTools& tools, const SubtitleJob& job,
	const SubtitleProgress& progress, const std::atomic_bool& cancel, std::wstring& error) {
	const auto& tr = tools.translate;
	const WhisperModel* model = FindWhisperModel(job.model);
	if (!model) {
		error = L"Unknown model: " + job.model;
		return false;
	}

	// 1. the model, downloaded once
	if (!IsWhisperModelPresent(tools, *model)) {
		const std::wstring what = tr(L"Downloading speech model") + L" (" +
			Megabytes(model->size) + L")";
		progress(what, 0);
		int lastPercent = -1;
		if (!NSHttpDownload::Download(MODEL_URL + std::wstring(model->file),
			ModelPath(tools, *model), model->size,
			[&](std::uint64_t received, std::uint64_t total) {
				const int percent = total ? static_cast<int>(received * 100 / total) : 0;
				if (percent != lastPercent) {
					lastPercent = percent;
					progress(what + L": " + Megabytes(received), percent);
				}
			}, cancel, error))
			return false;
	}
	const auto vadPath = tools.workDirectory / L"models" / VAD_FILE;
	std::error_code vadError;
	if (job.speechOnly && (std::filesystem::file_size(vadPath, vadError) != VAD_SIZE || vadError)) {
		progress(tr(L"Downloading speech model") + L" (1 MB)", 0);
		if (!NSHttpDownload::Download(VAD_URL + std::wstring(VAD_FILE), vadPath, VAD_SIZE,
			[](std::uint64_t, std::uint64_t) {}, cancel, error))
			return false;
	}

	// 2. a scratch folder with an ASCII path relative to the work directory:
	// whisper-cli reads its arguments in the ANSI code page
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto scratch = tools.workDirectory / L"tmp" / std::to_wstring(stamp);
	std::error_code ignored;
	std::filesystem::create_directories(scratch, ignored);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code ignored;
			std::filesystem::remove_all(path, ignored);
		}
	} cleanup{ scratch };

	// 3. the audio as 16 kHz mono WAV, which is what Whisper listens to
	progress(tr(L"Extracting audio..."), 0);
	double duration = 0;
	std::vector<std::string> ffmpegOutput;
	const std::wstring ffmpeg = NSChildProcess::Quote((tools.toolsDirectory / L"ffmpeg.exe").wstring()) +
		L" -hide_banner -nostdin -y -i " + NSChildProcess::Quote(job.input.wstring()) +
		L" -vn -ac 1 -ar 16000 -c:a pcm_s16le -progress pipe:1 -nostats audio.wav";
	const int ffmpegExit = NSChildProcess::Run(ffmpeg, scratch.wstring(),
		[&](const std::string& line) {
			ffmpegOutput.push_back(line);
			const size_t at = line.find("Duration: ");
			if (at != std::string::npos && duration == 0)
				duration = ParseClock(line.substr(at + 10));
			if (line.rfind("out_time_us=", 0) == 0 && duration > 0) {
				const double done = std::atof(line.c_str() + 12) / 1e6;
				progress(tr(L"Extracting audio..."),
					std::clamp(static_cast<int>(done * 10 / duration), 0, 10));
			}
		}, cancel, error);
	if (ffmpegExit != 0) {
		if (error.empty()) {
			bool noAudio = false;
			for (const auto& line : ffmpegOutput)
				noAudio |= line.find("does not contain any stream") != std::string::npos ||
				line.find("matches no streams") != std::string::npos;
			error = noAudio ? tr(L"The file has no audio track.")
				: tr(L"Cannot read the audio of this file.") + LastLines(ffmpegOutput, 3);
		}
		return false;
	}

	// 4. recognition - of the speech VAD finds, for speech; segments of at most
	// two subtitle lines; word times by DTW (needs flash attention off) into the JSON
	const unsigned threads = (std::max)(1u, (std::min)(8u, std::thread::hardware_concurrency()));
	std::wstring arguments = L" -m " + NSChildProcess::Quote(L"..\\..\\models\\" + std::wstring(model->file));
	if (job.speechOnly)
		arguments += L" --vad -vm " + NSChildProcess::Quote(L"..\\..\\models\\" + std::wstring(VAD_FILE));
	arguments += L" -dtw " + std::wstring(model->dtw) + L" -nfa" +
		L" -f audio.wav -of subtitles -ojf -pp -sow -ml " + std::to_wstring(LINE_LENGTH * 2) +
		L" -t " + std::to_wstring(threads) + L" -l " + job.language;
	if (job.translate)
		arguments += L" -tr";

	// With the GPU on, the Vulkan build first: ggml takes a discrete GPU, else
	// an integrated one (which can be slower than the CPU - the user's choice),
	// else the CPU, as fast as whisper-cli.exe. Without vulkan-1.dll (no GPU
	// driver) it does not start, and a GPU driver may fail it - then the CPU
	// build. CPUs without AVX2 get the compat build only.
	std::vector<const wchar_t*> engines;
	std::error_code ignoredError;
	if (NSCpuFeatures::HasAvx2()) {
		if (job.useGpu && HasGpuEngine(tools))
			engines.push_back(L"whisper-cli-vulkan.exe");
		engines.push_back(L"whisper-cli.exe");
	}
	else
		engines.push_back(L"whisper-cli-compat.exe");

	const std::wstring recognizing = tr(L"Recognizing speech...");
	std::vector<std::string> whisperOutput;
	int whisperExit = -1;
	for (const wchar_t* engine : engines) {
		progress(recognizing, 10);
		whisperOutput.clear();
		error.clear();
		std::filesystem::remove(scratch / L"subtitles.json", ignoredError);
		int percent = 10;
		std::wstring lastText;
		whisperExit = NSChildProcess::Run(
			NSChildProcess::Quote((tools.toolsDirectory / engine).wstring()) + arguments, scratch.wstring(),
			[&](const std::string& line) {
				whisperOutput.push_back(line);
				const size_t at = line.find("progress =");
				if (at != std::string::npos) {
					percent = 10 + std::clamp(std::atoi(line.c_str() + at + 10), 0, 100) * 9 / 10;
					progress(recognizing + lastText, percent);
					return;
				}
				// "[00:00:01.000 --> 00:00:03.200]  text": show what was just heard
				const size_t close = line.find("]  ");
				if (line.rfind("[", 0) == 0 && close != std::string::npos) {
					lastText = L"\r\n" + Trim(Widen(line.substr(close + 3)));
					progress(recognizing + lastText, percent);
				}
			}, cancel, error);
		if (whisperExit == 0 || cancel)
			break;
	}
	if (whisperExit != 0) {
		if (error.empty())
			error = tr(L"Speech recognition failed.") + LastLines(whisperOutput, 3);
		return false;
	}

	// 5. cues from the word times, tidied, saved next to the input
	std::ifstream produced(scratch / L"subtitles.json", std::ios::binary);
	const std::string json((std::istreambuf_iterator<char>(produced)), std::istreambuf_iterator<char>());
	std::string srt;
	if (!SrtFromJson(json, job.speechOnly, SpeechSpans(whisperOutput), srt)) {
		error = tr(L"Speech recognition failed.") + LastLines(whisperOutput, 3);
		return false;
	}
	const std::string tidy = TidySrt(srt);
	if (tidy.empty()) {
		error = tr(L"No speech was found in this file.");
		return false;
	}
	std::ofstream saved(job.output, std::ios::binary | std::ios::trunc);
	// UTF-8 with a BOM: Windows players read anything else as the ANSI code page
	saved << "\xEF\xBB\xBF" << tidy;
	if (!saved) {
		error = tr(L"Cannot write") + L" " + job.output.wstring();
		return false;
	}
	progress(tr(L"Subtitles are ready"), 100);
	return true;
}

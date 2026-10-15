// stems.exe - the Stem Splitter plugin's separation engine: Demucs v4 as ONNX,
// run by ONNX Runtime, with demucs.onnx's STFT code around it.
//
//   stems <model.onnx> <input.f32> <output dir> <threads> [vocals]
//
// input.f32 is raw float32 interleaved stereo at 44.1 kHz (what ffmpeg
// -f f32le -ac 2 -ar 44100 writes). Every source the model has is written as
// <name>.f32 in the same format. With "vocals" only vocals.f32 and
// instrumental.f32 (the mix minus the vocals) are written - for the
// fine-tuned vocals model, whose other three outputs are not its strength.
//
// The audio is cut into jobs with 0.75 s of context on each side, separated a
// few at a time - one ONNX session, <threads> split between the jobs in flight -
// and written straight to disk, so memory does not grow with the length of the
// file. Progress goes to stdout as "progress NN", errors to stderr in UTF-8.
// Paths may hold any characters.
//
// Every read and write names its own file offset (ReadFile/WriteFile with an
// OVERLAPPED offset): FILE* seek-then-write from several threads lost whole
// jobs of a track now and then.

#include "demucs.hpp"
#include "separate.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
	constexpr int SAMPLE_RATE = 44100;
	constexpr long long MAX_JOB_SAMPLES = 30LL * SAMPLE_RATE;
	constexpr long long MIN_JOB_SAMPLES = 8LL * SAMPLE_RATE;      // one Demucs segment
	constexpr long long CONTEXT_SAMPLES = SAMPLE_RATE * 3 / 4;  // 0.75 s each side

	const char* const SOURCES_4[] = { "drums", "bass", "other", "vocals" };
	const char* const SOURCES_6[] = { "drums", "bass", "other", "vocals", "guitar", "piano" };

	struct File {
		HANDLE handle = INVALID_HANDLE_VALUE;
		File() = default;
		File(const File&) = delete;
		File& operator=(const File&) = delete;
		~File() {
			if (handle != INVALID_HANDLE_VALUE)
				CloseHandle(handle);
		}
	};

	// I/O at an explicit offset, so threads never share a file position;
	// split into pieces a DWORD can count
	bool Positional(HANDLE handle, long long offset, char* data, size_t bytes, bool write) {
		while (bytes > 0) {
			OVERLAPPED at{};
			at.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
			at.OffsetHigh = static_cast<DWORD>(offset >> 32);
			const DWORD piece = static_cast<DWORD>((std::min)(bytes, size_t(1) << 30));
			DWORD done = 0;
			const BOOL ok = write ? WriteFile(handle, data, piece, &done, &at)
				: ReadFile(handle, data, piece, &done, &at);
			if (!ok || done == 0)
				return false;
			offset += done;
			data += done;
			bytes -= done;
		}
		return true;
	}

	// Every Run in flight holds about 2.5 GB, whatever its thread count. Past
	// the free memory Windows grows the page file on C: by the difference - 8
	// parallel Runs took 19 GB and left C: full. 3 Runs of 4 threads were the
	// fastest on 6 cores / 12 threads (a 4 min song: 112 s at 7.4 GB, against
	// 127 s at 14.7 GB for 6 Runs of 1), and never more than free memory carries.
	constexpr int MAX_PARALLEL_RUNS = 3;
	constexpr unsigned long long RUN_BYTES = 2800ULL << 20;
	constexpr unsigned long long SPARE_BYTES = 1536ULL << 20;  // for everything else

	int ParallelRuns(int threads) {
		int runs = (std::min)(MAX_PARALLEL_RUNS, threads);
		MEMORYSTATUSEX memory{};
		memory.dwLength = sizeof(memory);
		if (GlobalMemoryStatusEx(&memory)) {
			// both physical memory and commit: the page file grows on the latter
			const unsigned long long free = (std::min)(memory.ullAvailPhys, memory.ullAvailPageFile);
			const unsigned long long fit = free > SPARE_BYTES ? (free - SPARE_BYTES) / RUN_BYTES : 0;
			runs = (std::min)(runs, static_cast<int>((std::min)(fit, 64ULL)));
		}
		return (std::max)(1, runs);
	}

	std::string Utf8(const std::wstring& text) {
		if (text.empty())
			return {};
		const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0,
			nullptr, nullptr);
		std::string result(length, '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), length, nullptr,
			nullptr);
		return result;
	}

	// a streambuf that drops everything: demucs.onnx reports to std::cout
	struct NullBuffer : std::streambuf {
		int overflow(int c) override { return c; }
	};

	void Progress(int percent) {
		std::printf("progress %d\n", percent);
		std::fflush(stdout);
	}

	bool ReadSpan(const File& input, long long start, long long count, long long total,
		Eigen::MatrixXf& span) {
		// samples outside the file repeat the nearest edge
		span.resize(2, count);
		const long long first = (std::max)(0LL, start);
		const long long last = (std::min)(total, start + count);
		std::vector<float> buffer(static_cast<size_t>((last - first) * 2));
		if (!Positional(input.handle, first * 2 * (long long)sizeof(float),
			reinterpret_cast<char*>(buffer.data()), buffer.size() * sizeof(float), false))
			return false;
		for (long long index = 0; index < count; ++index) {
			const long long sample = (std::min)((std::max)(start + index, first), last - 1) - first;
			span(0, index) = buffer[static_cast<size_t>(sample * 2)];
			span(1, index) = buffer[static_cast<size_t>(sample * 2 + 1)];
		}
		return true;
	}

	bool WriteSpan(const File& output, long long start, std::vector<float>& interleaved) {
		return Positional(output.handle, start * 2 * (long long)sizeof(float),
			reinterpret_cast<char*>(interleaved.data()), interleaved.size() * sizeof(float), true);
	}
} // namespace

int wmain(int argc, wchar_t** argv) {
	if (argc < 5) {
		std::fprintf(stderr, "usage: stems <model.onnx> <input.f32> <output dir> <threads> [vocals]\n");
		return 2;
	}
	const std::wstring modelPath = argv[1], inputPath = argv[2], outputDir = argv[3];
	const int threads = (std::max)(1, _wtoi(argv[4]));
	const bool vocalsOnly = argc > 5 && std::wstring(argv[5]) == L"vocals";
	NullBuffer silence;
	std::cout.rdbuf(&silence);

	File input;
	input.handle = CreateFileW(inputPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	LARGE_INTEGER size{};
	if (input.handle == INVALID_HANDLE_VALUE || !GetFileSizeEx(input.handle, &size)) {
		std::fprintf(stderr, "error: cannot open %s\n", Utf8(inputPath).c_str());
		return 1;
	}
	const long long total = size.QuadPart / (2 * (long long)sizeof(float));
	if (total <= 0) {
		std::fprintf(stderr, "error: no audio in %s\n", Utf8(inputPath).c_str());
		return 1;
	}

	// a job per thread, 8-30 s each; more jobs than Runs keeps the Runs busy
	// to the end
	const long long jobSamples = (std::max)(MIN_JOB_SAMPLES,
		(std::min)(MAX_JOB_SAMPLES, (total + threads - 1) / threads));
	const long long jobs = (total + jobSamples - 1) / jobSamples;
	const int workers = static_cast<int>((std::min)(static_cast<long long>(ParallelRuns(threads)), jobs));

	demucsonnx::demucs_model model;
	try {
		std::ifstream file(modelPath, std::ios::binary);
		std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (bytes.empty())
			throw std::runtime_error("cannot read the model file");
		Ort::SessionOptions options;
		// the threads not spent on parallel jobs go into each Run
		options.SetIntraOpNumThreads((std::max)(1, threads / workers));
		options.SetInterOpNumThreads(1);
		options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		// the arena grows in doubling chunks; with several Runs at once that
		// asks for one huge block now and then and fails - plain malloc does not
		options.DisableCpuMemArena();
		if (!demucsonnx::load_model(bytes, model, options))
			throw std::runtime_error("not a Demucs model");
	}
	catch (const std::exception& error) {
		std::fprintf(stderr, "error: cannot load %s: %s\n", Utf8(modelPath).c_str(), error.what());
		return 1;
	}

	std::vector<std::string> names;
	if (vocalsOnly)
		names = { "vocals", "instrumental" };
	else if (model.nb_sources == 4)
		names.assign(std::begin(SOURCES_4), std::end(SOURCES_4));
	else
		names.assign(std::begin(SOURCES_6), std::end(SOURCES_6));
	std::vector<File> outputs(names.size());
	for (size_t index = 0; index < names.size(); ++index) {
		const std::wstring path = outputDir + L"\\" + std::wstring(names[index].begin(), names[index].end()) +
			L".f32";
		outputs[index].handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (outputs[index].handle == INVALID_HANDLE_VALUE) {
			std::fprintf(stderr, "error: cannot write %s\n", Utf8(path).c_str());
			return 1;
		}
	}

	std::atomic<long long> nextJob{ 0 };
	std::atomic<bool> failed{ false };
	std::string failure;
	std::mutex progressLock;
	std::vector<float> jobProgress(static_cast<size_t>(jobs), 0.0f);
	int printed = -1;
	auto report = [&](long long job, float fraction) {
		std::lock_guard<std::mutex> guard(progressLock);
		jobProgress[static_cast<size_t>(job)] = fraction;
		float sum = 0;
		for (float value : jobProgress)
			sum += value;
		const int percent = static_cast<int>(sum * 100 / jobs);
		if (percent > printed) {
			printed = percent;
			Progress(percent);
		}
	};

	auto worker = [&](unsigned seed) {
		std::mt19937 random(seed);
		long long job = -1;
		const char* stage = "starting";
		try {
			while (!failed && (job = nextJob++) < jobs) {
				const long long start = job * jobSamples;
				const long long length = (std::min)(jobSamples, total - start);
				Eigen::MatrixXf span;
				stage = "reading";
				if (!ReadSpan(input, start - CONTEXT_SAMPLES, length + 2 * CONTEXT_SAMPLES, total, span))
					throw std::runtime_error("cannot read " + Utf8(inputPath));
				stage = "separating";
				const Eigen::Tensor3dXf separated = SeparateSpan(model, span, random,
					[&](float fraction) { report(job, fraction); });

				stage = "writing";
				std::vector<float> interleaved(static_cast<size_t>(length * 2));
				for (size_t output = 0; output < names.size(); ++output) {
					for (long long index = 0; index < length; ++index)
						for (int channel = 0; channel < 2; ++channel) {
							const Eigen::Index at = CONTEXT_SAMPLES + index;
							float value;
							if (!vocalsOnly)
								value = separated(static_cast<Eigen::Index>(output), channel, at);
							else if (output == 0)
								value = separated(3, channel, at);  // vocals
							else  // instrumental: the mix without the vocals
								value = span(channel, at) - separated(3, channel, at);
							interleaved[static_cast<size_t>(index * 2 + channel)] = value;
						}
					if (!WriteSpan(outputs[output], start, interleaved))
						throw std::runtime_error("cannot write " + names[output] + ".f32 (Windows error " +
							std::to_string(GetLastError()) + ")");
				}
				report(job, 1.0f);
			}
		}
		catch (const std::exception& error) {
			std::lock_guard<std::mutex> guard(progressLock);
			if (!failed.exchange(true))
				failure = std::string(error.what()) + " (job " + std::to_string(job) + ", " + stage + ")";
		}
	};
	std::vector<std::thread> pool;
	for (int index = 0; index < workers; ++index)
		pool.emplace_back(worker, static_cast<unsigned>(1234 + index));
	for (auto& thread : pool)
		thread.join();

	if (failed) {
		std::fprintf(stderr, "error: %s\n", failure.c_str());
		return 1;
	}
	Progress(100);
	return 0;
}

#pragma once

#include <atomic>
#include <functional>
#include <string>

// Runs a bundled command line tool (ffmpeg, whisper-cli, ...) for a plugin:
// no console window, output read line by line, and cancellable - cancelling
// kills the process and everything it started.
namespace NSChildProcess
{
	// One argument quoted for a Windows command line (CommandLineToArgvW rules).
	std::wstring Quote(const std::wstring& argument);

	// Runs commandLine in workingDirectory. Every line the process writes to
	// stdout or stderr goes to onLine as raw bytes (UTF-8 for most tools);
	// lines split on \n and on \r, which progress output uses to rewrite one
	// line. cancel is polled. Returns the exit code, or -1 with error set when
	// the process could not start or was cancelled.
	int Run(const std::wstring& commandLine, const std::wstring& workingDirectory,
		const std::function<void(const std::string&)>& onLine,
		const std::atomic_bool& cancel, std::wstring& error);
}

#include "ChildProcess.h"

#include <windows.h>
#include <vector>

namespace NSChildProcess
{
	std::wstring Quote(const std::wstring& argument)
	{
		if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
			return argument;

		std::wstring quoted = L"\"";
		for (size_t index = 0;; ++index)
		{
			size_t backslashes = 0;
			while (index < argument.size() && argument[index] == L'\\')
			{
				++index;
				++backslashes;
			}
			if (index == argument.size())
			{
				// backslashes before the closing quote are doubled
				quoted.append(backslashes * 2, L'\\');
				break;
			}
			if (argument[index] == L'"')
			{
				quoted.append(backslashes * 2 + 1, L'\\');
				quoted.push_back(L'"');
			}
			else
			{
				quoted.append(backslashes, L'\\');
				quoted.push_back(argument[index]);
			}
		}
		quoted.push_back(L'"');
		return quoted;
	}

	namespace
	{
		struct Handle
		{
			HANDLE value = nullptr;
			~Handle() { Close(); }
			void Close()
			{
				if (value && value != INVALID_HANDLE_VALUE)
					CloseHandle(value);
				value = nullptr;
			}
		};

		void Split(std::string& pending, const char* data, size_t size,
			const std::function<void(const std::string&)>& onLine)
		{
			pending.append(data, size);
			size_t start = 0;
			for (size_t index = 0; index < pending.size(); ++index)
			{
				if (pending[index] != '\n' && pending[index] != '\r')
					continue;
				if (index > start)
					onLine(pending.substr(start, index - start));
				start = index + 1;
			}
			pending.erase(0, start);
		}
	}

	int Run(const std::wstring& commandLine, const std::wstring& workingDirectory,
		const std::function<void(const std::string&)>& onLine,
		const std::atomic_bool& cancel, std::wstring& error)
	{
		SECURITY_ATTRIBUTES inheritable{ sizeof(inheritable), nullptr, TRUE };
		Handle read, write, input;
		if (!CreatePipe(&read.value, &write.value, &inheritable, 0) ||
			!SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0))
		{
			error = L"Cannot create a pipe (error " + std::to_wstring(GetLastError()) + L")";
			return -1;
		}
		input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			&inheritable, OPEN_EXISTING, 0, nullptr);

		// the job takes the tool and anything it starts down with it
		Handle job;
		job.value = CreateJobObjectW(nullptr, nullptr);
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (job.value)
			SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits,
				sizeof(limits));

		STARTUPINFOW startup{ sizeof(startup) };
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdInput = input.value;
		startup.hStdOutput = write.value;
		startup.hStdError = write.value;
		PROCESS_INFORMATION process{};
		std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
		mutableCommand.push_back(L'\0');
		if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
			workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process))
		{
			error = L"Cannot start " + commandLine + L" (error " +
				std::to_wstring(GetLastError()) + L")";
			return -1;
		}
		Handle processHandle, threadHandle;
		processHandle.value = process.hProcess;
		threadHandle.value = process.hThread;
		if (job.value)
			AssignProcessToJobObject(job.value, process.hProcess);
		ResumeThread(process.hThread);
		// only the child holds the write end now, so ReadFile ends when it exits
		write.Close();

		std::string pending;
		char buffer[4096];
		bool cancelled = false;
		for (;;)
		{
			if (cancel && !cancelled)
			{
				cancelled = true;
				if (job.value)
					TerminateJobObject(job.value, 1);
				else
					TerminateProcess(process.hProcess, 1);
			}
			DWORD available = 0;
			if (!PeekNamedPipe(read.value, nullptr, 0, nullptr, &available, nullptr))
				break;  // the child closed its end: it has exited
			if (available == 0)
			{
				// exited with nothing left to read: done, even if something it
				// started still holds the pipe open
				if (WaitForSingleObject(process.hProcess, 50) == WAIT_OBJECT_0 &&
					PeekNamedPipe(read.value, nullptr, 0, nullptr, &available, nullptr) &&
					available == 0)
					break;
				continue;
			}
			DWORD got = 0;
			if (!ReadFile(read.value, buffer, sizeof(buffer), &got, nullptr) || got == 0)
				break;
			Split(pending, buffer, got, onLine);
		}
		if (!pending.empty())
			onLine(pending);

		WaitForSingleObject(process.hProcess, INFINITE);
		DWORD exitCode = 0;
		GetExitCodeProcess(process.hProcess, &exitCode);
		if (cancelled)
		{
			error = L"Cancelled";
			return -1;
		}
		return static_cast<int>(exitCode);
	}
}

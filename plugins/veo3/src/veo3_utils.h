#pragma once
#include <atomic>
#include <codecvt>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace NSSystemUtils
{
	std::wstring GetEnvVariable(const std::wstring& strName);
	std::wstring GetTempDirectory();
	std::wstring CreateTempFileWithUniqueName(const std::wstring& strFolderPathRoot, const std::wstring& Prefix);

	bool WriteWStringToUtf8File(const std::wstring& text, const std::wstring& filename, const bool& isBOM = true);
	std::wstring ReadWStringFromUtf8File(const std::wstring& filename);

	bool ExistsFile(const std::wstring& strFileName);
	bool RemoveFile(const std::wstring& strFileName);
} // namespace NSSystemUtils

namespace NSStringUtils
{
	void correctJsonPS(std::string& text);
} // namespace NSStringUtils

namespace NSCriticalSection
{
	class CCriticalSectionNative
	{
	private:
		::CRITICAL_SECTION m_oCs{};

	public:
		virtual void Enter();
		virtual void Leave();

		static void InitializeCriticalSection(NSCriticalSection::CCriticalSectionNative* pCsNative);
		static void DeleteCriticalSection(NSCriticalSection::CCriticalSectionNative* pCsNative);
	};

	class CCriticalSection
	{
	private:
		CCriticalSectionNative* m_pCsNative;

	public:
		CCriticalSection();
		~CCriticalSection();

		void InitializeCriticalSection();
		void DeleteCriticalSection();

		void Enter();
		void Leave();
	};

	class CTemporaryCriticalSection
	{
	public:
		CTemporaryCriticalSection(NSCriticalSection::CCriticalSection* pCs);
		~CTemporaryCriticalSection();
		void Leave();
		void Enter(NSCriticalSection::CCriticalSection* pCs);

	protected:
		NSCriticalSection::CCriticalSection* m_pCs;
	};
} // namespace NSCriticalSection

namespace NSThreads
{
	void Sleep(int milliseconds);
}

namespace NSProcesses
{
	enum class StreamType
	{
		StdOut,
		StdErr,
		Stop,
		Terminate
	};

	class CProcessRunnerCallback
	{
	public:
		CProcessRunnerCallback() = default;
		virtual ~CProcessRunnerCallback() = default;
		virtual void ProcessCallback(const int& id, const StreamType& type, const std::string& message) = 0;
	};

	class CProcessRunner
	{
	public:
		CProcessRunner(const int& id, const std::wstring& command, std::map<std::wstring, std::wstring>&& map,
			CProcessRunnerCallback* cb);
		~CProcessRunner();

		int GetId();
		void SetEnded();
		void Start();
		void Stop();
		void Wait();
		void WriteStdIn(const std::string& data);

	private:
		void ReadOutLoop(HANDLE handle, const StreamType& type);
		void Run();

		std::wstring GetPathVariable();
		std::map<std::wstring, std::wstring> GetEnv();
		std::wstring FindBinary(const std::wstring& cmd);

		std::wstring m_command;
		std::map<std::wstring, std::wstring> m_env;

		CProcessRunnerCallback* m_callback;
		std::thread m_worker;
		std::atomic<bool> m_running;
		int m_id;
		bool m_isEnded;

		PROCESS_INFORMATION m_pi{};
		HANDLE m_hJob{ nullptr };
		HANDLE m_hStdOutRd{ nullptr }, m_hStdOutWr{ nullptr };
		HANDLE m_hStdErrRd{ nullptr }, m_hStdErrWr{ nullptr };
		HANDLE m_hStdInRd{ nullptr }, m_hStdInWr{ nullptr };
	};

	class CProcessManager : public CProcessRunnerCallback
	{
	private:
		std::vector<CProcessRunner*> m_processes;
		int m_counter;
		std::atomic<bool> m_enableCallback;
		CProcessRunnerCallback* m_callback;

		NSCriticalSection::CCriticalSection m_cs;
		NSCriticalSection::CCriticalSection m_csCallback;

	public:
		CProcessManager(CProcessRunnerCallback* cb);
		~CProcessManager();

		int Start(const std::wstring& command, std::map<std::wstring, std::wstring>&& env);
		void End(const int& id);
		void WaitWhileWorked();
		void SendStdIn(const int& id, const std::string& data);
		void StopAll();
		virtual void ProcessCallback(const int& id, const StreamType& type, const std::string& message);
		int GetTasksCount();
	};
} // namespace NSProcesses
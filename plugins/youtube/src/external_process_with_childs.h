#pragma once
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <sstream>

#include <windows.h>
#include <winbase.h>
#include <processthreadsapi.h>

namespace NSSystemUtils
{
	std::wstring GetEnvVariable(const std::wstring& strName)
	{
		wchar_t* pVal = nullptr;
		size_t len = 0;

		if (_wdupenv_s(&pVal, &len, strName.c_str()) == 0 && pVal != nullptr)
		{
			std::wstring sRes(pVal);
			free(pVal);
			return sRes;
		}

		return L"";
	}
}

namespace NSCriticalSection
{
	class CRITICAL_SECTION_NATIVE
	{
	private:
		::CRITICAL_SECTION m_cs;

	public:
		virtual void Enter()
		{
			EnterCriticalSection(&m_cs);
		}
		virtual void Leave()
		{
			LeaveCriticalSection(&m_cs);
		}

	public:
		static void InitializeCriticalSection(NSCriticalSection::CRITICAL_SECTION_NATIVE* pNative)
		{
			::InitializeCriticalSection(&pNative->m_cs);
		}
		static void DeleteCriticalSection(NSCriticalSection::CRITICAL_SECTION_NATIVE* pNative)
		{
			::DeleteCriticalSection(&pNative->m_cs);
		}
	};

	class CRITICAL_SECTION
	{
	private:
		CRITICAL_SECTION_NATIVE* m_pCS;

	public:
		CRITICAL_SECTION()
		{
			m_pCS = new CRITICAL_SECTION_NATIVE();
		}
		~CRITICAL_SECTION()
		{
			delete m_pCS;
		}

		void InitializeCriticalSection()
		{
			CRITICAL_SECTION_NATIVE::InitializeCriticalSection(m_pCS);
		}
		void DeleteCriticalSection()
		{
			CRITICAL_SECTION_NATIVE::DeleteCriticalSection(m_pCS);
		}

		void Enter()
		{
			if (NULL != m_pCS)
				m_pCS->Enter();
		}
		void Leave()
		{
			if (NULL != m_pCS)
				m_pCS->Leave();
		}
	};

	class CTemporaryCS
	{
	public:
		CTemporaryCS(NSCriticalSection::CRITICAL_SECTION* cs)
		{
			cs->Enter();
			m_cs = cs;
		}
		~CTemporaryCS()
		{
			LeaveCS();
		}
		void LeaveCS()
		{
			if (NULL != m_cs)
				m_cs->Leave();
			m_cs = NULL;
		}
		void EnterCS(NSCriticalSection::CRITICAL_SECTION* cs)
		{
			LeaveCS();
			cs->Enter();
			m_cs = cs;
		}
	protected:
		NSCriticalSection::CRITICAL_SECTION* m_cs;
	};
}

namespace NSThreads
{
	void Sleep(int milliseconds) 
	{
		std::this_thread::sleep_for(std::chrono::microseconds(milliseconds));
	}
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
		CProcessRunnerCallback(){}
		virtual ~CProcessRunnerCallback(){}

		virtual void process_callback(const int& id, const StreamType& type, const std::string& message) = 0;
	};

	class CProcessRunner
	{
	public:
		CProcessRunner(const int& id, const std::wstring& command, std::map<std::wstring, std::wstring>&& map, CProcessRunnerCallback* cb)
			: m_command(command), m_env(map), m_callback(cb), m_running(false), m_id(id), m_isEnded(false)
		{
		}

		~CProcessRunner()
		{
			stop();
		}

		int get_id()
		{
			return m_id;
		}

		void set_ended()
		{
			m_isEnded = true;
		}

		void start()
		{
			if (m_running)
				return;

			m_running = true;

			m_worker = std::thread([this]() {
				run();
			});
		}

		void stop()
		{
			if (m_running.load())
			{
				if (m_hJob)
					CloseHandle(m_hJob);
				m_hJob = nullptr;

				if (m_hStdOutRd) { CloseHandle(m_hStdOutRd); m_hStdOutRd = nullptr; }
				if (m_hStdOutWr) { CloseHandle(m_hStdOutWr); m_hStdOutWr = nullptr; }
				if (m_hStdErrRd) { CloseHandle(m_hStdErrRd); m_hStdErrRd = nullptr; }
				if (m_hStdErrWr) { CloseHandle(m_hStdErrWr); m_hStdErrWr = nullptr; }
				if (m_hStdInRd)  { CloseHandle(m_hStdInRd);  m_hStdInRd  = nullptr; }
				if (m_hStdInWr)  { CloseHandle(m_hStdInWr);  m_hStdInWr  = nullptr; }
			}

			m_running.store(false);
			if (m_worker.joinable())
			{
				if (std::this_thread::get_id() != m_worker.get_id())
					m_worker.join();
				else
					m_worker.detach();
			}
		}

		void wait()
		{
			WaitForSingleObject(m_pi.hProcess, INFINITE);
		}

		void write_stdin(const std::string& data)
		{
			if (m_hStdInWr)
			{
				DWORD written = 0;
				WriteFile(m_hStdInWr, data.c_str(), static_cast<DWORD>(data.size()), &written, nullptr);
			}
		}

	private:

		void readOutLoop(HANDLE handle, const StreamType& type)
		{
			std::string lineBuf;
			char buf[4096];
			
			DWORD n;
			while (m_running.load())
			{
				DWORD available = 0;
				PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr);

				if (available == 0)
				{
					NSThreads::Sleep(100);
					continue;
				}

				if (!ReadFile(handle, buf, sizeof(buf), &n, nullptr) || n == 0)
					break;

				lineBuf.append(buf, n);
				size_t pos = 0;

				while (!lineBuf.empty()) 
				{
					size_t nextPos = lineBuf.find_first_of("\r\n", pos);

					if (nextPos != std::string::npos) 
					{
						std::string line = lineBuf.substr(0, nextPos);
						if (!line.empty()) 
						{
							m_callback->process_callback(m_id, type, line);
						}
						lineBuf.erase(0, nextPos + 1);
						if (!lineBuf.empty() && lineBuf[0] == '\n') 
						{
							lineBuf.erase(0, 1);
						}
					}
					else 
					{
						break;
					}
				}
			}

			if (!lineBuf.empty())
				m_callback->process_callback(m_id, type, lineBuf);
		}

		std::wstring getPathVariable()
		{
			std::wstring pathEnv = NSSystemUtils::GetEnvVariable(L"PATH");
			std::wstring systemEnv = L"";
			std::wstring userEnv = L"";

			if (true)
			{
				HKEY hKey;
				if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
				{
					DWORD size = 0;
					RegQueryValueExW(hKey, L"PATH", nullptr, nullptr, nullptr, &size);

					if (size > 0)
					{
						int charCount = (size / sizeof(wchar_t)) + 1;
						wchar_t* buffer = new wchar_t[charCount];
						if (RegQueryValueExW(hKey, L"PATH", nullptr, nullptr, (LPBYTE)buffer, &size) == ERROR_SUCCESS)
						{
							buffer[charCount - 1] = '\0';
							userEnv = std::wstring(buffer);
						}
						delete [] buffer;
					}

					RegCloseKey(hKey);
				}
			}

			if (true)
			{
				HKEY hKey;
				if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
				{
					DWORD size = 0;
					RegQueryValueExW(hKey, L"PATH", nullptr, nullptr, nullptr, &size);

					if (size > 0)
					{
						int charCount = (size / sizeof(wchar_t)) + 1;
						wchar_t* buffer = new wchar_t[charCount];
						if (RegQueryValueExW(hKey, L"PATH", nullptr, nullptr, (LPBYTE)buffer, &size) == ERROR_SUCCESS)
						{
							buffer[charCount - 1] = '\0';
							systemEnv = std::wstring(buffer);
						}
						delete [] buffer;
					}

					RegCloseKey(hKey);
				}
			}

			std::wstring result;

			if (!userEnv.empty())
				result = userEnv;

			if (!systemEnv.empty())
			{
				if (!result.empty())
					result += L";";

				result += systemEnv;
			}

			if (!pathEnv.empty())
			{
				if (!result.empty())
					result += L";";

				result += pathEnv;
			}

			return result;
		}

		std::map<std::wstring, std::wstring> getEnv()
		{
			std::map<std::wstring, std::wstring> env;

			wchar_t* envStrings = GetEnvironmentStringsW();
			if (!envStrings)
				return env;

			wchar_t* current = envStrings;

			while (*current != L'\0')
			{
				size_t len = wcslen(current) + 1; // +1 for \0

				std::wstring all(current, len - 1);
				auto pos = all.find('=');
				if (pos != std::wstring::npos)
				{
					std::wstring keyName = all.substr(0, pos);
					std::wstring value = all.substr(pos + 1);

					if (keyName == L"PATH" || keyName == L"Path")
					{
						std::wstring systemPath = getPathVariable();

						if (!systemPath.empty())
						{
							if (!value.empty())
								value += L";";
							value += systemPath;
						}
					}

					if (!keyName.empty())
						env[keyName] = value;
				}

				current += len;
			}

			FreeEnvironmentStringsW(envStrings);

			return env;
		}

		std::wstring findBinary(const std::wstring& cmd)
		{
			if (cmd.empty())
				return cmd;

			// https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/path
			std::vector<std::wstring> extensions = {L".exe", L".com", L".bat", L".cmd", L""};

			if (true)
			{
				for (const auto& ext : extensions)
				{
					std::wstring test = cmd + ext;
					DWORD attr = GetFileAttributesW(test.c_str());

					if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
						return test;
				}
			}

			std::wstring pathEnv = getPathVariable();
			std::wistringstream iss(pathEnv);
			std::wstring dir;

			while (std::getline(iss, dir, L';'))
			{
				if (dir.empty())
					continue;

				if (!dir.empty() && dir.front() == '"')
					dir = dir.substr(1);
				if (!dir.empty() && dir.back() == '"')
					dir.pop_back();

				if (!dir.empty() && dir.back() != '\\' && dir.back() != '/')
					dir += L"\\";

				// Try each executable extension in turn
				for (const auto& ext : extensions)
				{
					std::wstring test = dir + cmd + ext;
					DWORD attr = GetFileAttributesW(test.c_str());

					if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
						return test;
				}
			}

			return cmd;
		}

		void run()
		{
			SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};

			CreatePipe(&m_hStdOutRd, &m_hStdOutWr, &sa, 0);
			SetHandleInformation(m_hStdOutRd, HANDLE_FLAG_INHERIT, 0);

			CreatePipe(&m_hStdErrRd, &m_hStdErrWr, &sa, 0);
			SetHandleInformation(m_hStdErrRd, HANDLE_FLAG_INHERIT, 0);

			CreatePipe(&m_hStdInRd, &m_hStdInWr, &sa, 0);
			SetHandleInformation(m_hStdInWr, HANDLE_FLAG_INHERIT, 0);

			STARTUPINFOW si{};
			memset(&si, 0, sizeof(si));
			si.cb = sizeof(si);
			si.hStdInput  = m_hStdInRd;
			si.hStdOutput = m_hStdOutWr;
			si.hStdError = m_hStdErrWr;
			si.dwFlags |= STARTF_USESTDHANDLES;

			std::map<std::wstring, std::wstring> env = getEnv();
			for (auto& kv : m_env)
			{
				env[kv.first] = kv.second;
			}

			env[L"LANG"] = L"C.UTF-8";

			std::wstring envBlock;
			for (auto& kv : env)
			{
				envBlock += kv.first + L"=" + kv.second;
				envBlock.push_back(L'\0');
			}
			envBlock.push_back(L'\0');

			std::wstring commandW = m_command;
			std::wstring prog = commandW;

			std::wstring::size_type posProg = commandW.find(L" ");
			if (prog.length() > 2 && prog.front() == L'"')
			{
				posProg = commandW.find(L"\"", 1) + 1;
			}

			if (posProg != std::wstring::npos)
				prog = commandW.substr(0, posProg);
			prog = findBinary(prog);
			if (prog == commandW)
				prog = L"";

			if (prog.length() > 2 && prog.front() == L'"' && prog.back() == L'"')
			{
				prog = prog.substr(1, prog.size() - 2);
			}

			if (!CreateProcessW(prog.empty() ? nullptr : prog.c_str(), (LPWSTR)commandW.c_str(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
								(LPVOID)(envBlock.c_str()), nullptr, &si, &m_pi))
			{
				DWORD dwError = GetLastError();

				CloseHandle(m_hStdOutWr);
				CloseHandle(m_hStdErrWr);
				CloseHandle(m_hStdInRd);
				m_hStdOutWr = nullptr;
				m_hStdErrWr = nullptr;
				m_hStdInRd = nullptr;

				m_callback->process_callback(m_id, StreamType::Terminate, "");
				return;
			}

			m_hJob = CreateJobObject(nullptr, nullptr);
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
			jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(m_hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
			AssignProcessToJobObject(m_hJob, m_pi.hProcess);

			ResumeThread(m_pi.hThread);

			CloseHandle(m_hStdOutWr);
			CloseHandle(m_hStdErrWr);
			CloseHandle(m_hStdInRd);
			m_hStdOutWr = nullptr;
			m_hStdErrWr = nullptr;
			m_hStdInRd = nullptr;

			std::thread t_out = std::thread([this]()
											{
												readOutLoop(m_hStdOutRd, StreamType::StdOut);
											});
			std::thread t_err = std::thread([this]()
											{
												readOutLoop(m_hStdErrRd, StreamType::StdErr);
											});

			wait();

			m_running.store(false);

			if (t_out.joinable())
				t_out.join();
			if (t_err.joinable())
				t_err.join();

			m_callback->process_callback(m_id, m_isEnded ? StreamType::Terminate : StreamType::Stop, "");
		}

	private:
		std::wstring			m_command;
		std::map<std::wstring, std::wstring> m_env;

		CProcessRunnerCallback* m_callback;
		std::thread				m_worker;
		std::atomic<bool>		m_running;
		int						m_id;

		bool					m_isEnded;

		PROCESS_INFORMATION m_pi{};
		HANDLE m_hJob{nullptr};
		HANDLE m_hStdOutRd{nullptr}, m_hStdOutWr{nullptr};
		HANDLE m_hStdErrRd{nullptr}, m_hStdErrWr{nullptr};
		HANDLE m_hStdInRd{ nullptr },  m_hStdInWr{ nullptr };
	};

	class CProcessManager : public CProcessRunnerCallback
	{
	private:
		std::vector<CProcessRunner*> m_processes;
		int							 m_counter;
		std::atomic<bool>			 m_enable_callback;
		CProcessRunnerCallback*		 m_callback;

		NSCriticalSection::CRITICAL_SECTION m_cs;
		NSCriticalSection::CRITICAL_SECTION m_cs_callback;

	public:
		CProcessManager(CProcessRunnerCallback* cb)
		{
			m_cs.InitializeCriticalSection();
			m_cs_callback.InitializeCriticalSection();
			m_counter = 1;
			m_enable_callback.store(true);
			m_callback = cb;
		}
		~CProcessManager()
		{
			StopAll();
			m_cs_callback.DeleteCriticalSection();
			m_cs.DeleteCriticalSection();
		}

		int Start(const std::wstring& command, std::map<std::wstring, std::wstring>&& env)
		{
			m_cs.Enter();
			int cur_id = m_counter++;
			CProcessRunner* runner = new CProcessRunner(cur_id, command, std::move(env), this);
			m_processes.push_back(runner);
			m_cs.Leave();
			runner->start();
			return cur_id;
		}

		void End(const int& id)
		{
			NSCriticalSection::CTemporaryCS oCS(&m_cs);

			for (std::vector<CProcessRunner*>::iterator iter = m_processes.begin(); iter != m_processes.end(); iter++)
			{
				CProcessRunner* runner = *iter;
				if (runner->get_id() == id)
				{
					m_processes.erase(iter);
					runner->set_ended();
					delete runner;
					return;
				}
			}
		}

		void WaitWhileWorked()
		{
			while (true)
			{
				NSCriticalSection::CTemporaryCS oCS(&m_cs);
				if (m_processes.empty())
					break;

				NSThreads::Sleep(1000);
			}
		}

		void SendStdIn(const int& id, const std::string& data)
		{
			NSCriticalSection::CTemporaryCS oCS(&m_cs);

			for (std::vector<CProcessRunner*>::iterator iter = m_processes.begin(); iter != m_processes.end(); iter++)
			{
				CProcessRunner* runner = *iter;
				if (runner->get_id() == id)
				{
					runner->write_stdin(data);
					return;
				}
			}
		}

		void StopAll()
		{
			m_enable_callback.store(false);

			NSCriticalSection::CTemporaryCS oCS(&m_cs);
			for (std::vector<CProcessRunner*>::iterator iter = m_processes.begin(); iter != m_processes.end(); iter++)
			{
				CProcessRunner* runner = *iter;
				delete runner;
			}
			m_processes.clear();
		}

		virtual void process_callback(const int& id, const StreamType& type, const std::string& message)
		{
			NSCriticalSection::CTemporaryCS oCS(&m_cs_callback);

			if (!m_enable_callback.load())
				return;

			m_callback->process_callback(id, type == StreamType::Terminate ? StreamType::Stop : type, message);

			if (type == StreamType::Stop)
				End(id);
		}
	};
}

namespace NSSystemUtils
{
	std::wstring GetTempDirectory()
	{
		wchar_t pBuffer[MAX_PATH + 1];
		memset(pBuffer, 0, sizeof(wchar_t) * (MAX_PATH + 1));
		::GetTempPathW(MAX_PATH, pBuffer);

		std::wstring sRet(pBuffer);

		size_t nSeparatorPos = sRet.find_last_of(wchar_t('/'));
		if (std::wstring::npos == nSeparatorPos)
		{
			nSeparatorPos = sRet.find_last_of(wchar_t('\\'));
		}

		if (std::wstring::npos == nSeparatorPos)
			return L"";

		return sRet.substr(0, nSeparatorPos);
	}

	std::wstring CreateTempFileWithUniqueName(const std::wstring& strFolderPathRoot, const std::wstring& Prefix)
	{
		wchar_t pBuffer[MAX_PATH + 1];
		::GetTempFileNameW(strFolderPathRoot.c_str(), Prefix.c_str(), 0, pBuffer);
		std::wstring sRet(pBuffer);
		return sRet;
	}

	bool ExistsFile(const std::wstring& strFileName)
	{
		FILE* pFile = NULL;
		if (NULL == (pFile = _wfsopen(strFileName.c_str(), L"rb", _SH_DENYNO)))
			return false;

		if (NULL != pFile)
		{
			fclose(pFile);
			return true;
		}
		else
			return false;
	}

	bool RemoveFile(const std::wstring& strFileName)
	{
		return 0 == _wremove(strFileName.c_str());
	}
}
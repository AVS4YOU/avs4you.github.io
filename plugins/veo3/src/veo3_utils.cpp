#include "veo3_utils.h"
#include "../../../sdk/common/utils.h"

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

	bool WriteWStringToUtf8File(const std::wstring& text, const std::wstring& filename, const bool& isBOM)
	{
		HANDLE hFile =
			CreateFileW(filename.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (hFile == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		DWORD written = 0;
		if (isBOM)
		{
			const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
			if (!WriteFile(hFile, bom, sizeof(bom), &written, nullptr))
			{
				CloseHandle(hFile);
				return false;
			}
		}

		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		std::string utf8str = converter.to_bytes(text);

		if (!WriteFile(hFile, utf8str.data(), static_cast<DWORD>(utf8str.size()), &written, nullptr))
		{
			CloseHandle(hFile);
			return false;
		}

		CloseHandle(hFile);
		return true;
	}

	std::wstring ReadWStringFromUtf8File(const std::wstring& filename)
	{
		HANDLE hFile = CreateFileW(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);

		if (hFile == INVALID_HANDLE_VALUE)
		{
			return L"";
		}

		DWORD fileSize = GetFileSize(hFile, NULL);
		if (fileSize == INVALID_FILE_SIZE)
		{
			CloseHandle(hFile);
			return L"";
		}

		std::string buffer(fileSize, '\0');

		DWORD bytesRead = 0;
		BOOL success = ReadFile(hFile, &buffer[0], fileSize, &bytesRead, NULL);

		CloseHandle(hFile);

		if (!success || bytesRead == 0)
			return L"";

		std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
		return conv.from_bytes(buffer);
	}
} // namespace NSSystemUtils

namespace NSStringUtils
{
	void correctJsonPS(std::string& text)
	{
		replace(text, "(\\u0026)", "&");
		replace(text, "(\\u003c)", "<");
		replace(text, "(\\u003e)", ">");
		replace(text, "(\\u0027)", "'");
		replace(text, "(\\u0022)", "\"");
		replace(text, "(\\u002b)", "+");
		replace(text, "(\\u003d)", "=");
	}
} // namespace NSStringUtils

namespace NSFileCoding
{
	std::vector<char> EncodeFileToBase64(const std::wstring& filePath)
	{
		static constexpr char BASE64_TABLE[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz"
			"0123456789+/";

		std::ifstream file(filePath, std::ios::binary);
		if (!file)
			throw std::runtime_error("Failed to open file");

		file.seekg(0, std::ios::end);
		std::streamsize fileSize = file.tellg();
		file.seekg(0, std::ios::beg);

		std::vector<uint8_t> input(static_cast<size_t>(fileSize));

		if (fileSize > 0)
		{
			if (!file.read(reinterpret_cast<char*>(input.data()), fileSize))
				throw std::runtime_error("Failed to read file");
		}

		std::vector<char> output;
		output.reserve(((input.size() + 2) / 3) * 4);

		size_t i = 0;

		while (i + 2 < input.size())
		{
			uint32_t value =
				(static_cast<uint32_t>(input[i]) << 16) |
				(static_cast<uint32_t>(input[i + 1]) << 8) |
				(static_cast<uint32_t>(input[i + 2]));

			output.push_back(BASE64_TABLE[(value >> 18) & 0x3F]);
			output.push_back(BASE64_TABLE[(value >> 12) & 0x3F]);
			output.push_back(BASE64_TABLE[(value >> 6) & 0x3F]);
			output.push_back(BASE64_TABLE[value & 0x3F]);

			i += 3;
		}

		size_t remain = input.size() - i;

		if (remain == 1)
		{
			uint32_t value = static_cast<uint32_t>(input[i]) << 16;

			output.push_back(BASE64_TABLE[(value >> 18) & 0x3F]);
			output.push_back(BASE64_TABLE[(value >> 12) & 0x3F]);
			output.push_back('=');
			output.push_back('=');
		}
		else if (remain == 2)
		{
			uint32_t value =
				(static_cast<uint32_t>(input[i]) << 16) |
				(static_cast<uint32_t>(input[i + 1]) << 8);

			output.push_back(BASE64_TABLE[(value >> 18) & 0x3F]);
			output.push_back(BASE64_TABLE[(value >> 12) & 0x3F]);
			output.push_back(BASE64_TABLE[(value >> 6) & 0x3F]);
			output.push_back('=');
		}

		return output;
	}
}

namespace NSCriticalSection
{
	void CCriticalSectionNative::Enter()
	{
		EnterCriticalSection(&m_oCs);
	}
	void CCriticalSectionNative::Leave()
	{
		LeaveCriticalSection(&m_oCs);
	}

	void CCriticalSectionNative::InitializeCriticalSection(NSCriticalSection::CCriticalSectionNative* pCsNative)
	{
		::InitializeCriticalSection(&pCsNative->m_oCs);
	}
	void CCriticalSectionNative::DeleteCriticalSection(NSCriticalSection::CCriticalSectionNative* pCsNative)
	{
		::DeleteCriticalSection(&pCsNative->m_oCs);
	}

	CCriticalSection::CCriticalSection()
	{
		m_pCsNative = new CCriticalSectionNative();
	}
	CCriticalSection::~CCriticalSection()
	{
		delete m_pCsNative;
	}

	void CCriticalSection::InitializeCriticalSection()
	{
		CCriticalSectionNative::InitializeCriticalSection(m_pCsNative);
	}
	void CCriticalSection::DeleteCriticalSection()
	{
		CCriticalSectionNative::DeleteCriticalSection(m_pCsNative);
	}

	void CCriticalSection::Enter()
	{
		if (NULL != m_pCsNative)
			m_pCsNative->Enter();
	}
	void CCriticalSection::Leave()
	{
		if (NULL != m_pCsNative)
			m_pCsNative->Leave();
	}

	CTemporaryCriticalSection::CTemporaryCriticalSection(NSCriticalSection::CCriticalSection* pCs)
	{
		pCs->Enter();
		m_pCs = pCs;
	}
	CTemporaryCriticalSection::~CTemporaryCriticalSection()
	{
		Leave();
	}
	void CTemporaryCriticalSection::Leave()
	{
		if (NULL != m_pCs)
			m_pCs->Leave();
		m_pCs = NULL;
	}
	void CTemporaryCriticalSection::Enter(NSCriticalSection::CCriticalSection* pCs)
	{
		Leave();
		pCs->Enter();
		m_pCs = pCs;
	}
} // namespace NSCriticalSection

namespace NSThreads
{
	void Sleep(int milliseconds)
	{
		std::this_thread::sleep_for(std::chrono::microseconds(milliseconds));
	}
} // namespace NSThreads

namespace NSProcesses
{
	CProcessRunner::CProcessRunner(const int& id, const std::wstring& command,
		std::map<std::wstring, std::wstring>&& map, CProcessRunnerCallback* cb)
		: m_command(command)
		, m_env(map)
		, m_callback(cb)
		, m_running(false)
		, m_id(id)
		, m_isEnded(false)
	{
	}

	CProcessRunner::~CProcessRunner()
	{
		Stop();
	}

	int CProcessRunner::GetId()
	{
		return m_id;
	}

	void CProcessRunner::SetEnded()
	{
		m_isEnded = true;
	}

	void CProcessRunner::Start()
	{
		if (m_running)
			return;

		m_running = true;

		m_worker = std::thread([this]() { Run(); });
	}

	void CProcessRunner::Stop()
	{
		if (m_running.load())
		{
			if (m_hJob)
				CloseHandle(m_hJob);
			m_hJob = nullptr;

			if (m_hStdOutRd)
			{
				CloseHandle(m_hStdOutRd);
				m_hStdOutRd = nullptr;
			}
			if (m_hStdOutWr)
			{
				CloseHandle(m_hStdOutWr);
				m_hStdOutWr = nullptr;
			}
			if (m_hStdErrRd)
			{
				CloseHandle(m_hStdErrRd);
				m_hStdErrRd = nullptr;
			}
			if (m_hStdErrWr)
			{
				CloseHandle(m_hStdErrWr);
				m_hStdErrWr = nullptr;
			}
			if (m_hStdInRd)
			{
				CloseHandle(m_hStdInRd);
				m_hStdInRd = nullptr;
			}
			if (m_hStdInWr)
			{
				CloseHandle(m_hStdInWr);
				m_hStdInWr = nullptr;
			}
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

	void CProcessRunner::Wait()
	{
		WaitForSingleObject(m_pi.hProcess, INFINITE);
	}

	void CProcessRunner::WriteStdIn(const std::string& data)
	{
		if (m_hStdInWr)
		{
			DWORD written = 0;
			WriteFile(m_hStdInWr, data.c_str(), static_cast<DWORD>(data.size()), &written, nullptr);
		}
	}

	void CProcessRunner::ReadOutLoop(HANDLE handle, const StreamType& type)
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
			size_t pos;
			while ((pos = lineBuf.find('\n')) != std::string::npos)
			{
				m_callback->ProcessCallback(m_id, type, lineBuf.substr(0, pos));
				lineBuf.erase(0, pos + 1);
			}
		}

		if (!lineBuf.empty())
			m_callback->ProcessCallback(m_id, type, lineBuf);
	}

	std::wstring CProcessRunner::GetPathVariable()
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
					delete[] buffer;
				}

				RegCloseKey(hKey);
			}
		}

		if (true)
		{
			HKEY hKey;
			if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
				0, KEY_READ, &hKey) == ERROR_SUCCESS)
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
					delete[] buffer;
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

	std::map<std::wstring, std::wstring> CProcessRunner::GetEnv()
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
					std::wstring systemPath = GetPathVariable();

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

	std::wstring CProcessRunner::FindBinary(const std::wstring& cmd)
	{
		if (cmd.empty())
			return cmd;

		// https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/path
		std::vector<std::wstring> extensions = { L".exe", L".com", L".bat", L".cmd", L"" };

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

		std::wstring pathEnv = GetPathVariable();
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

			// Проверяем с разными расширениями
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

	void CProcessRunner::Run()
	{
		SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };

		CreatePipe(&m_hStdOutRd, &m_hStdOutWr, &sa, 0);
		SetHandleInformation(m_hStdOutRd, HANDLE_FLAG_INHERIT, 0);

		CreatePipe(&m_hStdErrRd, &m_hStdErrWr, &sa, 0);
		SetHandleInformation(m_hStdErrRd, HANDLE_FLAG_INHERIT, 0);

		CreatePipe(&m_hStdInRd, &m_hStdInWr, &sa, 0);
		SetHandleInformation(m_hStdInWr, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOW si{};
		memset(&si, 0, sizeof(si));
		si.cb = sizeof(si);
		si.hStdInput = m_hStdInRd;
		si.hStdOutput = m_hStdOutWr;
		si.hStdError = m_hStdErrWr;
		si.dwFlags |= STARTF_USESTDHANDLES;

		std::map<std::wstring, std::wstring> env = GetEnv();
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
		if (posProg != std::wstring::npos)
			prog = commandW.substr(0, posProg);
		prog = FindBinary(prog);
		if (prog == commandW)
			prog = L"";

		if (!CreateProcessW(prog.empty() ? nullptr : prog.c_str(), (LPWSTR)commandW.c_str(), nullptr, nullptr, TRUE,
			CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
			(LPVOID)(envBlock.c_str()), nullptr, &si, &m_pi))
		{
			DWORD dwError = GetLastError();

			CloseHandle(m_hStdOutWr);
			CloseHandle(m_hStdErrWr);
			CloseHandle(m_hStdInRd);
			m_hStdOutWr = nullptr;
			m_hStdErrWr = nullptr;
			m_hStdInRd = nullptr;

			m_callback->ProcessCallback(m_id, StreamType::Terminate, "");
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

		std::thread t_out = std::thread([this]() { ReadOutLoop(m_hStdOutRd, StreamType::StdOut); });
		std::thread t_err = std::thread([this]() { ReadOutLoop(m_hStdErrRd, StreamType::StdErr); });

		Wait();

		m_running.store(false);

		if (t_out.joinable())
			t_out.join();
		if (t_err.joinable())
			t_err.join();

		m_callback->ProcessCallback(m_id, m_isEnded ? StreamType::Terminate : StreamType::Stop, "");
	}

	CProcessManager::CProcessManager(CProcessRunnerCallback* cb)
	{
		m_cs.InitializeCriticalSection();
		m_csCallback.InitializeCriticalSection();
		m_counter = 1;
		m_enableCallback.store(true);
		m_callback = cb;
	}
	CProcessManager::~CProcessManager()
	{
		StopAll();
		m_csCallback.DeleteCriticalSection();
		m_cs.DeleteCriticalSection();
	}

	int CProcessManager::Start(const std::wstring& command, std::map<std::wstring, std::wstring>&& env)
	{
		m_cs.Enter();
		int cur_id = m_counter++;
		CProcessRunner* runner = new CProcessRunner(cur_id, command, std::move(env), this);
		m_processes.push_back(runner);
		m_cs.Leave();
		runner->Start();
		return cur_id;
	}

	void CProcessManager::End(const int& id)
	{
		NSCriticalSection::CTemporaryCriticalSection oCS(&m_cs);

		for (std::vector<CProcessRunner*>::iterator iter = m_processes.begin(); iter != m_processes.end(); iter++)
		{
			CProcessRunner* runner = *iter;
			if (runner->GetId() == id)
			{
				m_processes.erase(iter);
				runner->SetEnded();
				delete runner;
				return;
			}
		}
	}

	void CProcessManager::WaitWhileWorked()
	{
		while (true)
		{
			NSCriticalSection::CTemporaryCriticalSection oCS(&m_cs);
			if (m_processes.empty())
				break;

			NSThreads::Sleep(1000);
		}
	}

	void CProcessManager::SendStdIn(const int& id, const std::string& data)
	{
		NSCriticalSection::CTemporaryCriticalSection oCS(&m_cs);

		for (std::vector<CProcessRunner*>::iterator iter = m_processes.begin(); iter != m_processes.end(); iter++)
		{
			CProcessRunner* runner = *iter;
			if (runner->GetId() == id)
			{
				runner->WriteStdIn(data);
				return;
			}
		}
	}

	void CProcessManager::StopAll()
	{
		m_enableCallback.store(false);

		NSCriticalSection::CTemporaryCriticalSection oCS(&m_cs);
		for (std::vector<CProcessRunner*>::iterator iter = m_processes.begin(); iter != m_processes.end(); iter++)
		{
			CProcessRunner* runner = *iter;
			delete runner;
		}
		m_processes.clear();
	}

	int CProcessManager::GetTasksCount()
	{
		NSCriticalSection::CTemporaryCriticalSection oCS(&m_cs);
		return (int)m_processes.size();
	}

	void CProcessManager::ProcessCallback(const int& id, const StreamType& type, const std::string& message)
	{
		NSCriticalSection::CTemporaryCriticalSection oCS(&m_csCallback);

		if (!m_enableCallback.load())
			return;

		m_callback->ProcessCallback(id, type == StreamType::Terminate ? StreamType::Stop : type, message);

		if (type == StreamType::Stop)
			End(id);
	}
} // namespace NSProcesses
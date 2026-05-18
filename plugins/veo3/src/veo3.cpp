#include "veo3.h"
#include "../../../sdk/common/utils.h"

CVeo3::~CVeo3()
{
	if (m_manager)
		delete m_manager;
}

std::wstring CVeo3::CreateWorkDirectory()
{
	std::wstring sValue = NSSystemUtils::CreateTempFileWithUniqueName(NSSystemUtils::GetTempDirectory(), L"AVS");
	if (NSSystemUtils::ExistsFile(sValue))
		NSSystemUtils::RemoveFile(sValue);

	CreateDirectoryW(sValue.c_str(), NULL);
	return sValue;
}

std::wstring CVeo3::GetCurrentDateTime()
{
	auto now = std::chrono::system_clock::now();
	std::time_t t = std::chrono::system_clock::to_time_t(now);

	std::tm localTime;
	localtime_s(&localTime, &t);

	std::wstringstream oss;
	oss << std::setfill(L'0') << std::setw(2) << localTime.tm_mday << "." << std::setw(2) << (localTime.tm_mon + 1)
		<< "." << (localTime.tm_year + 1900) << "_" << std::setw(2) << localTime.tm_hour << "." << std::setw(2)
		<< localTime.tm_min << "." << std::setw(2) << localTime.tm_sec;

	return oss.str();
}

void CVeo3::Process(NSProcesses::CProcessRunnerCallback* callback, const std::wstring& workDirectory)
{
	if (m_manager)
	{
		m_manager->StopAll();
		delete m_manager;
	}

	m_manager = new NSProcesses::CProcessManager(callback);

	std::wstring sCommand = SCRIPT;
	NSStringUtils::replace(sCommand, L"${PARAM_KEY}", m_key);
	NSStringUtils::replace(sCommand, L"${PARAM_SECONDS}", m_durationSeconds);
	NSStringUtils::replace(sCommand, L"${PARAM_RESOLUTION}", m_resolution);
	NSStringUtils::replace(sCommand, L"${PARAM_MODEL_NAME}", m_model);

	std::wstring sPath = L"Veo3_" + GetCurrentDateTime() + L".mp4";
	NSStringUtils::replace(sCommand, L"${PARAM_VIDEO_NAME}", sPath);
	NSStringUtils::replace(sCommand, L"${PARAM_VIDEO_LOG_FILE}", L".Veo3.log");

	std::wstring sWorkDirectory = workDirectory;
	if (sWorkDirectory.empty())
		sWorkDirectory = CreateWorkDirectory();

	std::wstring sWorkScript = sWorkDirectory + L"\\" + L"script.ps1";
	
	NSSystemUtils::WriteWStringToUtf8File(sCommand, sWorkScript);
	NSSystemUtils::WriteWStringToUtf8File(m_prompt, sWorkDirectory + L"\\" + L"script.prompt", false);

	m_file = sPath;
	m_start_time = 0;
	m_manager->Start(L"powershell -ExecutionPolicy Bypass -File \"" + sWorkScript + L"\"", {});
}

void CVeo3::FakeStart()
{
	if (0 != m_start_time)
		return;
	m_start_time = GetTickCount();
}

int CVeo3::GetFakeProgress()
{
	DWORD current_time = GetTickCount();
	DWORD current_progress = current_time - m_start_time;

	double count_video_seconds_in_realseconds = 0.075;

	// 10% => 90%
	double duration = (double)(std::stoi(m_durationSeconds) * 1000);
	double current_duration = current_progress * count_video_seconds_in_realseconds;

	int current_progress_int = (int)(80 * current_duration / duration);

	int progress = 10 + current_progress_int;
	if (progress > 90)
		return 90;

	return progress;
}

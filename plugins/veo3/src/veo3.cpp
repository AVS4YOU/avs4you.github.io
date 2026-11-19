#include "veo3.h"
#include "scripts_bodys.h"
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

	std::wstring sCommand;
	// "ai_<date>_<time>.mp4" (Veo3_ before 1.1.0); Extend finds a video by its content, not its name
	std::wstring sPath = L"ai_" + GetCurrentDateTime() + L".mp4";
	std::wstring sWorkDirectory = workDirectory;
	if (sWorkDirectory.empty()) 
		sWorkDirectory = CreateWorkDirectory();
	std::wstring sWorkScript = sWorkDirectory + L"\\" + L"script.ps1";

	m_manager = new NSProcesses::CProcessManager(callback);

	// One script for all the modes (scripts_bodys.h). Every value goes into a single-quoted PowerShell
	// string, where only a ' has to be doubled and nothing else is expanded.
	auto quoted = [](std::wstring value)
	{
		NSStringUtils::replace(value, L"'", L"''");
		return value;
	};

	std::wstring mode = L"text";
	switch (m_generation_mode)
	{
	case NSGenerationMode::ImageToVideo:		mode = L"image"; break;
	case NSGenerationMode::FirstAndLastFrame:	mode = L"frames"; break;
	case NSGenerationMode::ExtendVideo:			mode = L"extend"; break;
	case NSGenerationMode::EditVideo:			mode = L"edit"; break;
	default: break;
	}
	const bool continues = m_generation_mode == NSGenerationMode::ExtendVideo || m_generation_mode == NSGenerationMode::EditVideo;

	sCommand = SCRIPT_OMNI;
	NSStringUtils::replace(sCommand, L"${PARAM_MODE}", mode);
	NSStringUtils::replace(sCommand, L"${PARAM_KEY}", quoted(m_key));
	NSStringUtils::replace(sCommand, L"${PARAM_SECONDS}", quoted(m_durationSeconds));
	NSStringUtils::replace(sCommand, L"${PARAM_RESOLUTION}", quoted(m_resolution));
	NSStringUtils::replace(sCommand, L"${PARAM_MODEL_NAME}", quoted(m_model));
	NSStringUtils::replace(sCommand, L"${PARAM_ASPECT_RATIO}", quoted(m_aspectRatio));

	NSStringUtils::replace(sCommand, L"${PARAM_VIDEO_NAME}", quoted(sPath));
	NSStringUtils::replace(sCommand, L"${PARAM_VIDEO_LOG_FILE}", L".Veo3.log");

	// the user's paths last, so nothing in them is taken for a placeholder
	NSStringUtils::replace(sCommand, L"${PARAM_PREVIOUS_ID}", quoted(continues ? m_previousInteractionId : L""));
	NSStringUtils::replace(sCommand, L"${PARAM_INPUT_VIDEO}",
		quoted(m_generation_mode == NSGenerationMode::EditVideo ? m_inputVideo : L""));
	// the pictures: references, the two frames, or the photos to put into an edited video
	const wchar_t* images[] = { L"${PARAM_IMAGE_1}", L"${PARAM_IMAGE_2}", L"${PARAM_IMAGE_3}" };
	for (size_t i = 0; i < 3; i++)
	{
		std::wstring image;
		if (m_generation_mode != NSGenerationMode::ExtendVideo && i < m_additional_files_paths.size())
			image = m_additional_files_paths[i];
		NSStringUtils::replace(sCommand, images[i], quoted(image));
	}

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
	DWORD elapsed_ms = current_time - m_start_time;
	
	int steps = elapsed_ms / 5000;
	int progress = 10 + steps * 10;

	if (progress > 90)
		progress = 90;

	return progress;
}

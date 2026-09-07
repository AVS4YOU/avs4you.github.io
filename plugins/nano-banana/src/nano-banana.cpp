#include "nano-banana.h"
#include "scripts_bodys.h"
#include "../../../sdk/common/utils.h"

CNanoBanana::~CNanoBanana()
{
	if (m_manager)
		delete m_manager;
}

std::wstring CNanoBanana::CreateWorkDirectory()
{
	std::wstring sValue = NSSystemUtils::CreateTempFileWithUniqueName(NSSystemUtils::GetTempDirectory(), L"AVS");
	if (NSSystemUtils::ExistsFile(sValue))
		NSSystemUtils::RemoveFile(sValue);

	CreateDirectoryW(sValue.c_str(), NULL);
	return sValue;
}

std::wstring CNanoBanana::GetCurrentDateTime()
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

void CNanoBanana::Process(NSProcesses::CProcessRunnerCallback* callback, const std::wstring& workDirectory)
{
	if (m_manager)
	{
		m_manager->StopAll();
		delete m_manager;
	}

	m_mimeType = L"image/jpeg";
	std::wstring sCommand = SCRIPT_NANO_BANANA;
	std::wstring sPath = L"NanoBanana_" + GetCurrentDateTime() + L".jpg";
	std::wstring sWorkDirectory = workDirectory;
	if (sWorkDirectory.empty())
		sWorkDirectory = CreateWorkDirectory();
	std::wstring sWorkScript = sWorkDirectory + L"\\" + L"script.ps1";
	std::wstring sPromptPath = sWorkDirectory + L"\\" + L"script.prompt";
	std::wstring sOutputPath = sWorkDirectory + L"\\" + sPath;
	std::wstring sLogPath = sWorkDirectory + L"\\" + L".NanoBanana.log";

	m_manager = new NSProcesses::CProcessManager(callback);

	std::vector<std::wstring> fields = {
		L"${PARAM_IMAGE_1}",
		L"${PARAM_IMAGE_2}",
		L"${PARAM_IMAGE_3}",
		L"${PARAM_IMAGE_4}",
		L"${PARAM_IMAGE_5}",
		L"${PARAM_IMAGE_6}"
	};

	for (size_t i = 0; i < fields.size(); i++)
	{
		std::wstring value = i < m_additional_files_paths.size() ? m_additional_files_paths[i] : L"";
		NSStringUtils::replace(sCommand, fields[i], value);
	}

	std::wstring mode = L"text_to_image";
	switch (m_generation_mode)
	{
	case NSGenerationMode::ImageEdit:
		mode = L"image_edit";
		break;
	case NSGenerationMode::MultiTurnEdit:
		mode = L"multi_turn";
		break;
	case NSGenerationMode::GoogleSearch:
		mode = L"google_search";
		break;
	case NSGenerationMode::ImageSearch:
		mode = L"image_search";
		break;
	case NSGenerationMode::VideoToImage:
		mode = L"video_to_image";
		break;
	default:
		break;
	}

	NSStringUtils::replace(sCommand, L"${PARAM_KEY}", m_key);
	NSStringUtils::replace(sCommand, L"${PARAM_MODEL_NAME}", m_model);
	NSStringUtils::replace(sCommand, L"${PARAM_ASPECT_RATIO}", m_aspectRatio);
	NSStringUtils::replace(sCommand, L"${PARAM_IMAGE_SIZE}", m_imageSize);
	NSStringUtils::replace(sCommand, L"${PARAM_MIME_TYPE}", m_mimeType);
	NSStringUtils::replace(sCommand, L"${PARAM_MODE}", mode);
	NSStringUtils::replace(sCommand, L"${PARAM_PREVIOUS_INTERACTION_ID}", m_previousInteractionId);
	NSStringUtils::replace(sCommand, L"${PARAM_VIDEO_URI}", m_videoUri);
	NSStringUtils::replace(sCommand, L"${PARAM_IMAGE_NAME}", sOutputPath);
	NSStringUtils::replace(sCommand, L"${PARAM_PROMPT_FILE}", sPromptPath);
	NSStringUtils::replace(sCommand, L"${PARAM_IMAGE_LOG_FILE}", sLogPath);

	NSSystemUtils::WriteWStringToUtf8File(sCommand, sWorkScript);
	NSSystemUtils::WriteWStringToUtf8File(m_prompt, sPromptPath, false);

	m_file = sPath;
	m_manager->Start(L"powershell -ExecutionPolicy Bypass -File \"" + sWorkScript + L"\"", {});
}

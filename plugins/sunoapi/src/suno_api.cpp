#include "suno_api.h"
#include <mutex>
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
namespace
{
std::wstring ErrorText(DWORD code)
{
	wchar_t *t = nullptr;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, (LPWSTR)&t, 0, nullptr);
	std::wstring r = t ? t : L"Network error";
	if (t)
		LocalFree(t);
	return r;
}
bool Crack(const std::wstring &in, URL_COMPONENTS &c, std::wstring &host, std::wstring &path)
{
	std::wstring url = in;
	if (url.find(L"://") == std::wstring::npos)
		url = std::wstring(L"https://api.sunoapi.org") + (url.empty() || url[0] == L'/' ? L"" : L"/") + url;
	wchar_t hb[512] = {}, pb[4096] = {};
	c = {};
	c.dwStructSize = sizeof(c);
	c.lpszHostName = hb;
	c.dwHostNameLength = _countof(hb);
	c.lpszUrlPath = pb;
	c.dwUrlPathLength = _countof(pb);
	c.dwSchemeLength = 1;
	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &c))
		return false;
	host.assign(hb, c.dwHostNameLength);
	path.assign(pb, c.dwUrlPathLength);
	if (path.empty())
		path = L"/";
	return true;
}
std::string ToUtf8(const std::wstring &value)
{
	if (value.empty())
		return {};
	int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	std::string result(size, 0);
	WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), &result[0], size, nullptr, nullptr);
	return result;
}

void LogApiResponse(const std::wstring &method, const std::wstring &pathOrUrl, const Suno::HttpResponse &response)
{
	const bool isApiRequest = pathOrUrl.find(L"://") == std::wstring::npos || pathOrUrl.find(L"api.sunoapi.org") != std::wstring::npos;

	wchar_t localAppData[MAX_PATH] = {};
	DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return;

	std::wstring directory = std::wstring(localAppData) + L"\\avs_plugin_sunoapi";
	CreateDirectoryW(directory.c_str(), nullptr);
	std::wstring logPath = directory + L"\\.sunoapi.log";

	SYSTEMTIME time{};
	GetLocalTime(&time);
	char timestamp[32] = {};
	sprintf_s(timestamp, "%04u-%02u-%02u %02u:%02u:%02u.%03u", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);

	std::string entry;
	entry += "[" + std::string(timestamp) + "] " + ToUtf8(method) + " " + ToUtf8(pathOrUrl) + "\r\n";
	entry += "HTTP status: " + std::to_string(response.status) + "\r\n";
	if (!response.error.empty())
		entry += "Network error: " + ToUtf8(response.error) + "\r\n";
	if (isApiRequest)
		entry += "Response body:\r\n" + response.body + "\r\n";
	else
		entry += "Response body: <binary omitted, " + std::to_string(response.body.size()) + " bytes>\r\n";
	entry += "------------------------------------------------------------\r\n";

	static std::mutex logMutex;
	std::lock_guard<std::mutex> lock(logMutex);
	HANDLE file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return;
	DWORD written = 0;
	WriteFile(file, entry.data(), static_cast<DWORD>(entry.size()), &written, nullptr);
	CloseHandle(file);
}
} // namespace
std::wstring Suno::Utf8ToWide(const std::string &v)
{
	if (v.empty())
		return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, v.data(), (int)v.size(), nullptr, 0);
	std::wstring r(n, 0);
	MultiByteToWideChar(CP_UTF8, 0, v.data(), (int)v.size(), &r[0], n);
	return r;
}
std::string Suno::WideToUtf8(const std::wstring &v)
{
	if (v.empty())
		return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, v.data(), (int)v.size(), nullptr, 0, nullptr, nullptr);
	std::string r(n, 0);
	WideCharToMultiByte(CP_UTF8, 0, v.data(), (int)v.size(), &r[0], n, nullptr, nullptr);
	return r;
}
Suno::HttpResponse Suno::Request(const std::wstring &key, const std::wstring &method, const std::wstring &pathOrUrl, const std::string &body)
{
	HttpResponse out;
	URL_COMPONENTS c{};
	std::wstring host, path;
	if (!Crack(pathOrUrl, c, host, path))
	{
		out.error = L"Invalid URL";
		LogApiResponse(method, pathOrUrl, out);
		return out;
	}
	HINTERNET s = WinHttpOpen(L"AVS4YOU-SunoApi/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!s)
	{
		out.error = ErrorText(GetLastError());
		LogApiResponse(method, pathOrUrl, out);
		return out;
	}
	WinHttpSetTimeouts(s, 30000, 30000, 30000, 120000);
	HINTERNET cn = WinHttpConnect(s, host.c_str(), c.nPort, 0);
	HINTERNET rq = cn ? WinHttpOpenRequest(cn, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, c.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
	std::wstring hs = L"Accept: application/json\r\n";
	if (!key.empty())
		hs += L"Authorization: Bearer " + key + L"\r\n";
	if (!body.empty())
		hs += L"Content-Type: application/json; charset=utf-8\r\n";
	BOOL ok = rq && WinHttpSendRequest(rq, hs.c_str(), (DWORD)-1, body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) && WinHttpReceiveResponse(rq, nullptr);
	if (!ok)
		out.error = ErrorText(GetLastError());
	if (ok)
	{
		DWORD z = sizeof(out.status);
		WinHttpQueryHeaders(rq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &out.status, &z, WINHTTP_NO_HEADER_INDEX);
		for (;;)
		{
			DWORD a = 0, n = 0;
			if (!WinHttpQueryDataAvailable(rq, &a) || !a)
				break;
			size_t old = out.body.size();
			out.body.resize(old + a);
			if (!WinHttpReadData(rq, &out.body[old], a, &n))
				break;
			out.body.resize(old + n);
		}
	}
	if (rq)
		WinHttpCloseHandle(rq);
	if (cn)
		WinHttpCloseHandle(cn);
	WinHttpCloseHandle(s);
	LogApiResponse(method, pathOrUrl, out);
	return out;
}
bool Suno::Download(const std::wstring &url, const std::wstring &target, std::wstring &error)
{
	HttpResponse response = Request(L"", L"GET", url);
	if (!response.error.empty() || response.status < 200 || response.status >= 300)
	{
		error = !response.error.empty() ? response.error : L"Download HTTP " + std::to_wstring(response.status);
		return false;
	}
	if (response.body.empty())
	{
		error = L"The server returned an empty file.";
		return false;
	}

	const std::wstring temporary = target + L".download";
	DeleteFileW(temporary.c_str());
	HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		error = ErrorText(GetLastError());
		return false;
	}

	bool writtenSuccessfully = true;
	size_t offset = 0;
	while (offset < response.body.size())
	{
		const size_t remaining = response.body.size() - offset;
		const DWORD requested = static_cast<DWORD>(remaining > 1024 * 1024 ? 1024 * 1024 : remaining);
		DWORD written = 0;
		if (!WriteFile(file, response.body.data() + offset, requested, &written, nullptr) || written != requested)
		{
			error = ErrorText(GetLastError());
			writtenSuccessfully = false;
			break;
		}
		offset += written;
	}

	if (writtenSuccessfully && !FlushFileBuffers(file))
	{
		error = ErrorText(GetLastError());
		writtenSuccessfully = false;
	}
	if (!CloseHandle(file))
	{
		if (writtenSuccessfully)
			error = ErrorText(GetLastError());
		writtenSuccessfully = false;
	}
	if (!writtenSuccessfully)
	{
		DeleteFileW(temporary.c_str());
		return false;
	}

	if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		error = ErrorText(GetLastError());
		DeleteFileW(temporary.c_str());
		return false;
	}

	LARGE_INTEGER expectedSize{};
	expectedSize.QuadPart = static_cast<LONGLONG>(response.body.size());
	for (int attempt = 0; attempt < 100; ++attempt)
	{
		HANDLE verification = CreateFileW(target.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (verification != INVALID_HANDLE_VALUE)
		{
			LARGE_INTEGER actualSize{};
			const bool ready = GetFileSizeEx(verification, &actualSize) && actualSize.QuadPart == expectedSize.QuadPart;
			CloseHandle(verification);
			if (ready)
			{
				Sleep(25);
				return true;
			}
		}
		Sleep(50);
	}

	error = L"The downloaded file remains locked or has an unexpected size.";
	return false;
}
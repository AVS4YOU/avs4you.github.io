#include "HttpDownload.h"

#include <windows.h>
#include <winhttp.h>
#include <fstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace NSHttpDownload
{
	namespace
	{
		struct Internet
		{
			HINTERNET value = nullptr;
			~Internet()
			{
				if (value)
					WinHttpCloseHandle(value);
			}
		};

		std::wstring LastError(const wchar_t* what)
		{
			return std::wstring(what) + L" (error " + std::to_wstring(GetLastError()) + L")";
		}
	}

	bool Download(const std::wstring& url, const std::filesystem::path& target,
		std::uint64_t expectedSize,
		const std::function<void(std::uint64_t, std::uint64_t)>& progress,
		const std::atomic_bool& cancel, std::wstring& error)
	{
		URL_COMPONENTS parts{ sizeof(parts) };
		wchar_t host[256]{};
		std::vector<wchar_t> path(url.size() + 1);
		parts.lpszHostName = host;
		parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
		parts.lpszUrlPath = path.data();
		parts.dwUrlPathLength = static_cast<DWORD>(path.size());
		if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
		{
			error = L"Invalid download address: " + url;
			return false;
		}

		Internet session, connection, request;
		session.value = WinHttpOpen(L"AVS4YOU plugin", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session.value)  // before Windows 8.1 there is no automatic proxy
			session.value = WinHttpOpen(L"AVS4YOU plugin", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
				WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (session.value)
			connection.value = WinHttpConnect(session.value, host, parts.nPort, 0);
		if (connection.value)
			request.value = WinHttpOpenRequest(connection.value, L"GET", path.data(), nullptr,
				WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
				parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
		if (!request.value ||
			!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
				WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
			!WinHttpReceiveResponse(request.value, nullptr))
		{
			error = LastError((L"Cannot connect to " + std::wstring(host)).c_str());
			return false;
		}

		DWORD status = 0, size = sizeof(status);
		WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
		if (status != 200)
		{
			error = L"Download failed: HTTP " + std::to_wstring(status) + L" for " + url;
			return false;
		}
		wchar_t lengthText[32]{};
		size = sizeof(lengthText);
		std::uint64_t total = 0;
		if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH,
			WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &size, WINHTTP_NO_HEADER_INDEX))
			total = _wcstoui64(lengthText, nullptr, 10);

		std::error_code ignored;
		std::filesystem::create_directories(target.parent_path(), ignored);
		const std::filesystem::path partial = target.wstring() + L".partial";
		{
			std::ofstream file(partial, std::ios::binary | std::ios::trunc);
			if (!file)
			{
				error = L"Cannot write " + partial.wstring();
				return false;
			}
			std::vector<char> buffer(1 << 16);
			std::uint64_t received = 0;
			for (;;)
			{
				if (cancel)
				{
					file.close();
					std::filesystem::remove(partial, ignored);
					error = L"Cancelled";
					return false;
				}
				DWORD got = 0;
				if (!WinHttpReadData(request.value, buffer.data(),
					static_cast<DWORD>(buffer.size()), &got))
				{
					error = LastError(L"Download interrupted");
					return false;
				}
				if (got == 0)
					break;
				file.write(buffer.data(), got);
				received += got;
				if (progress)
					progress(received, total);
			}
			if (!file)
			{
				error = L"Cannot write " + partial.wstring() + L" - is the disk full?";
				return false;
			}
		}

		const std::uint64_t written = std::filesystem::file_size(partial, ignored);
		if ((expectedSize && written != expectedSize) || (total && written != total))
		{
			std::filesystem::remove(partial, ignored);
			error = L"Download incomplete: " + url;
			return false;
		}
		std::error_code moved;
		std::filesystem::rename(partial, target, moved);
		if (moved)
		{
			error = L"Cannot move " + partial.wstring() + L" into place";
			return false;
		}
		return true;
	}
}

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

// Downloads a model or tool over HTTPS with WinHTTP - part of every Windows,
// no curl needed - following redirects (Hugging Face sends file downloads to
// its CDN).
namespace NSHttpDownload
{
	// Downloads url to target through target + ".partial", so an interrupted
	// download never looks complete. expectedSize, when not 0, must match.
	// progress gets (received, total); total is 0 when the server does not
	// say. cancel is polled.
	bool Download(const std::wstring& url, const std::filesystem::path& target,
		std::uint64_t expectedSize,
		const std::function<void(std::uint64_t, std::uint64_t)>& progress,
		const std::atomic_bool& cancel, std::wstring& error);
}

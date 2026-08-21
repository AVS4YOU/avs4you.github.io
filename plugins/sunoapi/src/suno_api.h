#pragma once
#include <string>
namespace Suno
{
struct HttpResponse
{
	int status = 0;
	std::string body;
	std::wstring error;
};
HttpResponse Request(const std::wstring &, const std::wstring &, const std::wstring &, const std::string & = {});
bool Download(const std::wstring &, const std::wstring &, std::wstring &);
std::wstring Utf8ToWide(const std::string &);
std::string WideToUtf8(const std::wstring &);
} // namespace Suno

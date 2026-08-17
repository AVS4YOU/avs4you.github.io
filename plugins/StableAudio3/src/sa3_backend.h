#pragma once
#include "plugin.h"
#include <functional>
bool GenerateAudio(CStableAudio3Plugin &plugin, const GenerationOptions &options, const std::filesystem::path &output,
                   const std::function<void(const std::wstring &, int)> &progress, std::wstring &error);
bool DownloadModelSet(CStableAudio3Plugin &plugin, const std::wstring &model, const std::wstring &encoding,
                      const std::function<void(const std::wstring &, int)> &progress, std::wstring &error);
bool IsModelSetPresent(const std::filesystem::path &models, const std::wstring &model, const std::wstring &encoding);

#pragma once
#include "plugin.h"

bool GenerateSpeech(SupertonicPlugin& plugin, const GenerationOptions& options,
                    std::wstring& error);

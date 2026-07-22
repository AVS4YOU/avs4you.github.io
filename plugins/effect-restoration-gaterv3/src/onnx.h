#pragma once

#include "../3dparty/include/onnxruntime/core/session/onnxruntime_cxx_api.h"
#ifdef _WIN64
#pragma comment(lib, "./libs/x64/onnxruntime.lib")
#else
#pragma comment(lib, "./libs/x86/onnxruntime.lib")
#endif // _WIN64


#include <string>

class COnnx
{
public:
	COnnx() = delete;
	COnnx(std::wstring wsModel, unsigned int threads = 1);
	COnnx(const COnnx&) = delete;
	COnnx(COnnx&&) = delete;
	virtual ~COnnx() {}

	void Apply(unsigned char* pixels, int w, int h, int tile = 256, int overlap = 24);

private:
	Ort::Env m_env;
	Ort::Session m_session;
	Ort::AllocatorWithDefaultOptions m_allocator;

	Ort::AllocatedStringPtr m_inputName;
	Ort::AllocatedStringPtr m_outputName;

	std::vector<Ort::Value> RunInference(std::vector<float>& input, int w, int h);
	
	static std::vector<float> ToTensor(unsigned char* pixels, int w, int h);
	static void FromTensor(unsigned char* outPixels, int w, int h, float* tensor);

	static Ort::SessionOptions GetSessionOptions(unsigned int threads = 1);
};
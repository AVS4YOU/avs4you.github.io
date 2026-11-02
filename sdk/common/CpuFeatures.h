#pragma once

namespace NSCpuFeatures
{
	// What our AVX2 ggml/ONNX builds need: AVX2 with FMA and F16C, and an OS
	// that saves the AVX registers. Without it a plugin runs its compat build.
	bool HasAvx2();
}

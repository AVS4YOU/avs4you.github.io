#include "CpuFeatures.h"

#include <immintrin.h>
#include <intrin.h>

namespace NSCpuFeatures
{
	bool HasAvx2()
	{
		int regs[4]{};
		__cpuid(regs, 0);
		if (regs[0] < 7)
			return false;
		__cpuid(regs, 1);
		const int ecx = regs[2];
		// OSXSAVE, and the OS saving the XMM and YMM registers on a context switch
		if (!(ecx & (1 << 27)) || (_xgetbv(0) & 6) != 6)
			return false;
		// AVX, FMA, F16C
		if (!(ecx & (1 << 28)) || !(ecx & (1 << 12)) || !(ecx & (1 << 29)))
			return false;
		__cpuidex(regs, 7, 0);
		return (regs[1] & (1 << 5)) != 0;  // AVX2
	}
}

#pragma once

#include <atomic>
#include "MIPS.h"

//The address of the memory access going through these proxies right now, with bit 0 set for a
//write, or 0 when none is. Read from another thread when a game freezes inside compiled code: the
//only way out of a block is through one of these calls.
extern std::atomic<uint32> g_memoryProxyAccess;

extern "C"
{
	uint32 MemoryUtils_GetByteProxy(CMIPS*, uint32);
	uint32 MemoryUtils_GetHalfProxy(CMIPS*, uint32);
	uint32 MemoryUtils_GetWordProxy(CMIPS*, uint32);
	uint64 MemoryUtils_GetDoubleProxy(CMIPS*, uint32);
	uint128 MemoryUtils_GetQuadProxy(CMIPS*, uint32);

	void MemoryUtils_SetByteProxy(CMIPS*, uint32, uint32);
	void MemoryUtils_SetHalfProxy(CMIPS*, uint32, uint32);
	void MemoryUtils_SetWordProxy(CMIPS*, uint32, uint32);
	void MemoryUtils_SetDoubleProxy(CMIPS*, uint64, uint32);
	void MemoryUtils_SetQuadProxy(CMIPS*, const uint128&, uint32);

	//The same 128-bit accesses for code generators that cannot pass 128-bit values: the register
	//is named by its offset in the CPU context.
	void MemoryUtils_LoadQuadProxy(CMIPS*, uint32 vAddress, uint32 registerOffset);
	void MemoryUtils_StoreQuadProxy(CMIPS*, uint32 registerOffset, uint32 vAddress);
}

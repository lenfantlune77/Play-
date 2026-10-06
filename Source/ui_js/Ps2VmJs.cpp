#include "Ps2VmJs.h"
#include "ee/Vu0Async.h"
#include "Jitter_CodeGen_Wasm.h"
#include "MemoryUtils.h"
#include "BasicBlock.h"
#include "PS2VM_Preferences.h"
#include "AppConfig.h"
#include "COP_SCU.h"
#include "MIPS.h"
#include "ee/PS2OS.h"
#include "ee/FpAddTruncate.h"

extern "C" uint32 LWL_Proxy(uint32, uint32, CMIPS*);
extern "C" uint32 LWR_Proxy(uint32, uint32, CMIPS*);
extern "C" uint64 LDL_Proxy(uint32, uint64, CMIPS*);
extern "C" uint64 LDR_Proxy(uint32, uint64, CMIPS*);
extern "C" void SWL_Proxy(uint32, uint32, CMIPS*);
extern "C" void SWR_Proxy(uint32, uint32, CMIPS*);
extern "C" void SDL_Proxy(uint32, uint64, CMIPS*);
extern "C" void SDR_Proxy(uint32, uint64, CMIPS*);
extern "C" void TrapHandler(CMIPS*);
extern "C" void HandleTLBException(CMIPS*);

// Every native function the MIPS JIT can call must be known to the WebAssembly code generator,
// with an exported C symbol: an unregistered call emits an invalid module (unbalanced stack).
// Static members and C++ functions get C wrappers; the registry is keyed by the original pointer.
extern "C" void HandleTLBRead_Proxy(CMIPS* context)
{
	CCOP_SCU::HandleTLBRead(context);
}

extern "C" void HandleTLBWrite_Proxy(CMIPS* context)
{
	CCOP_SCU::HandleTLBWrite(context);
}

extern "C" uint32 TranslateAddress_Proxy(CMIPS* context, uint32 address)
{
	return CPS2OS::TranslateAddress(context, address);
}

extern "C" uint32 TranslateAddressTLB_Proxy(CMIPS* context, uint32 address)
{
	return CPS2OS::TranslateAddressTLB(context, address);
}

extern "C" uint32 TranslateAddress64_Proxy(CMIPS* context, uint32 address)
{
	return CMIPS::TranslateAddress64(context, address);
}

extern "C" uint32 CheckTLBExceptions_Proxy(CMIPS* context, uint32 address, uint32 isWrite)
{
	return CPS2OS::CheckTLBExceptions(context, address, isWrite);
}

extern "C" uint32 FpAddTruncate_Proxy(uint32 a, uint32 b)
{
	return FpAddTruncate(a, b);
}

void CPs2VmJs::CreateVM()
{
	printf("Initializing PS2VM...\r\n");

	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&EmptyBlockHandler), "_EmptyBlockHandler", "vi");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_GetByteProxy), "_MemoryUtils_GetByteProxy", "iii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_GetHalfProxy), "_MemoryUtils_GetHalfProxy", "iii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_GetWordProxy), "_MemoryUtils_GetWordProxy", "iii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_GetDoubleProxy), "_MemoryUtils_GetDoubleProxy", "jii");

	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_SetByteProxy), "_MemoryUtils_SetByteProxy", "viii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_SetHalfProxy), "_MemoryUtils_SetHalfProxy", "viii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_SetWordProxy), "_MemoryUtils_SetWordProxy", "viii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_SetDoubleProxy), "_MemoryUtils_SetDoubleProxy", "viji");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_LoadQuadProxy), "_MemoryUtils_LoadQuadProxy", "viii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&MemoryUtils_StoreQuadProxy), "_MemoryUtils_StoreQuadProxy", "viii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&Vu0Async_Pull), "_Vu0Async_Pull", "vi");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&Vu0Async_Push), "_Vu0Async_Push", "vi");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&Vu0Async_Wait), "_Vu0Async_Wait", "vi");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&Vu0Async_Signal), "_Vu0Async_Signal", "vi");

	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&LWL_Proxy), "_LWL_Proxy", "iiii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&LWR_Proxy), "_LWR_Proxy", "iiii");

	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&LDL_Proxy), "_LDL_Proxy", "jiji");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&LDR_Proxy), "_LDR_Proxy", "jiji");

	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&SWL_Proxy), "_SWL_Proxy", "viii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&SWR_Proxy), "_SWR_Proxy", "viii");

	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&SDL_Proxy), "_SDL_Proxy", "viji");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&SDR_Proxy), "_SDR_Proxy", "viji");

	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&TrapHandler), "_TrapHandler", "vi");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&HandleTLBException), "_HandleTLBException", "vi");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&CCOP_SCU::HandleTLBRead), "_HandleTLBRead_Proxy", "vi");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&CCOP_SCU::HandleTLBWrite), "_HandleTLBWrite_Proxy", "vi");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&CPS2OS::TranslateAddress), "_TranslateAddress_Proxy", "iii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&CPS2OS::TranslateAddressTLB), "_TranslateAddressTLB_Proxy", "iii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&CMIPS::TranslateAddress64), "_TranslateAddress64_Proxy", "iii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&CPS2OS::CheckTLBExceptions), "_CheckTLBExceptions_Proxy", "iiii");
	Jitter::CWasmFunctionRegistry::RegisterFunction(reinterpret_cast<uintptr_t>(&FpAddTruncate), "_FpAddTruncate_Proxy", "iii");

	CPS2VM::CreateVM();
}

void CPs2VmJs::BootElf(std::string path)
{
	m_mailBox.SendCall([this, path]() {
		printf("Loading '%s'...\r\n", path.c_str());
		try
		{
			Reset();
			m_ee->m_os->BootFromFile(path);
		}
		catch(const std::exception& ex)
		{
			printf("Failed to start: %s.\r\n", ex.what());
			return;
		}
		printf("Starting...\r\n");
		ResumeImpl();
	});
}

void CPs2VmJs::BootDiscImage(std::string path)
{
	m_mailBox.SendCall([this, path]() {
		printf("Loading '%s'...\r\n", path.c_str());
		try
		{
			CAppConfig::GetInstance().SetPreferencePath(PREF_PS2_CDROM0_PATH, path);
			Reset();
			m_ee->m_os->BootFromCDROM();
		}
		catch(const std::exception& ex)
		{
			printf("Failed to start: %s.\r\n", ex.what());
			return;
		}
		printf("Starting...\r\n");
		ResumeImpl();
	});
}
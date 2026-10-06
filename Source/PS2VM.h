#pragma once

#include <thread>
#include <future>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <deque>
#include <atomic>
#include "filesystem_def.h"
#include "Types.h"
#include "MIPS.h"
#include "MailBox.h"
#include "PadHandler.h"
#include "ScreenPositionListener.h"
#include "OpticalMedia.h"
#include "VirtualMachine.h"
#include "ee/Ee_SubSystem.h"
#include "iop/Iop_SubSystem.h"
#include "sound/SoundHandler.h"
#include "FrameLimiter.h"
#include "Profiler.h"

class CPS2VM : public CVirtualMachine
{
public:
	struct CPU_UTILISATION_INFO
	{
		int32 eeTotalTicks = 0;
		int32 eeIdleTicks = 0;

		int32 iopTotalTicks = 0;
		int32 iopIdleTicks = 0;
	};

	typedef std::unique_ptr<COpticalMedia> OpticalMediaPtr;
	typedef std::unique_ptr<Ee::CSubSystem> EeSubSystemPtr;
	typedef std::unique_ptr<Iop::CSubSystem> IopSubSystemPtr;
	typedef Framework::CSignal<void()> NewFrameEvent;
	typedef std::function<void(CPS2VM*)> ExecutableReloadedHandler;

	CPS2VM();
	virtual ~CPS2VM() = default;

	void Initialize();
	void Destroy();

	void StepEe();
	void StepIop();
	void StepVu0();
	void StepVu1();

	void Resume() override;
	void Pause() override;
	void PauseAsync();
	void Reset(uint32 = PS2::EE_BASE_RAM_SIZE, uint32 = PS2::IOP_BASE_RAM_SIZE);

	STATUS GetStatus() const override;

	void CreateGSHandler(const CGSHandler::FactoryFunction&);
	CGSHandler* GetGSHandler();
	void DestroyGSHandler();

	void CreatePadHandler(const CPadHandler::FactoryFunction&);
	CPadHandler* GetPadHandler();
	void DestroyPadHandler();

	void CreateSoundHandler(const CSoundHandler::FactoryFunction&);
	CSoundHandler* GetSoundHandler();
	void ReloadSpuBlockCount();
	void DestroySoundHandler();

	void CDROM0_SyncPath();
	void CDROM0_Reset();

	void SetEeFrequencyScale(uint32, uint32);
	void ReloadFrameRateLimit();
	void SetNetplayInputApplyHandler(const std::function<void(uint64)>& handler);
	void SetNetplayInputReplayHandler(const std::function<void(uint64)>& handler);

	// Deterministic frame gate used by the browser two-core local multiplayer mode.
	void SetNetplayLockstep(bool enabled);
	void ResetNetplayFrame();
	uint64 GetNetplayFrame() const;
	void AdvanceNetplayFrame(uint64 frame);
	uint64 GetNetplayStateHash() const;
	bool SaveNetplayState(const fs::path&);
	bool LoadNetplayState(const fs::path&);
	bool CaptureNetplaySnapshot();
	bool RestoreNetplaySnapshot();
	uint64 GetNetplaySnapshotFrame() const;
	std::vector<uint8> GetNetplaySnapshot() const;
	void SetNetplaySnapshot(const std::vector<uint8>&, uint64 frame);
	bool RollbackNetplayFrame(uint64 frame);
	void ReplayNetplayFrames(uint64 targetFrame);

	static fs::path GetStateDirectoryPath();
	fs::path GenerateStatePath(unsigned int) const;

	std::future<bool> SaveState(const fs::path&);
	std::future<bool> LoadState(const fs::path&);

	CPU_UTILISATION_INFO GetCpuUtilisationInfo() const;

#ifdef DEBUGGER_INCLUDED
	fs::path MakeDebugTagsPackagePath(const char*);
	void LoadDebugTags(const char*);
	void SaveDebugTags(const char*);
#endif

	void ReportGunPosition(float, float);
	bool HasGunListener() const;
	void SetGunListener(CScreenPositionListener*);

	void ReportTouchPosition(float, float);
	bool HasTouchListener() const;
	void SetTouchListener(CScreenPositionListener*);
	void ReleaseScreenPosition();

	OpticalMediaPtr m_cdrom0;
	CPadHandler* m_pad = nullptr;

	EeSubSystemPtr m_ee;
	IopSubSystemPtr m_iop;

	//Read from another thread when a game stops drawing: how many times the emulation loop went
	//round, and which step it is in. A count that stands still means the loop is stuck in that step.
	enum EMU_STEP : uint32
	{
		EMU_STEP_LOOP,
		EMU_STEP_MAILBOX,
		EMU_STEP_SPU,
		EMU_STEP_VBLANK_START,
		EMU_STEP_VBLANK_END,
		EMU_STEP_EE,
		EMU_STEP_IOP,
	};
	std::atomic<uint32> m_emuLoops = 0;
	std::atomic<uint32> m_emuStep = EMU_STEP_LOOP;
	std::atomic<bool> m_netplayLockstep = false;
	std::atomic<uint64> m_netplayFrame = 0;
	std::atomic<uint64> m_netplayPermit = 0;
	std::atomic<uint64> m_netplayStateHash = 0;
	std::vector<uint8> m_netplaySnapshot;
	std::atomic<uint64> m_netplaySnapshotFrame = 0;
	struct NetplaySnapshotEntry
	{
		uint64 frame = 0;
		std::vector<uint8> bytes;
	};
	std::deque<NetplaySnapshotEntry> m_netplaySnapshots;
	mutable std::mutex m_netplayMutex;
	std::condition_variable m_netplayCondition;
	std::function<void(uint64)> m_netplayInputApplyHandler;
	std::function<void(uint64)> m_netplayInputReplayHandler;

	NewFrameEvent OnNewFrame;

	ExecutableReloadedHandler BeforeExecutableReloaded;
	ExecutableReloadedHandler AfterExecutableReloaded;

protected:
	virtual void CreateVM();
	void ResumeImpl();

	CMailBox m_mailBox;

private:
	void ValidateThreadContext();

	void ResetVM();
	void DestroyVM();
	bool SaveVMState(const fs::path&);
	bool LoadVMState(const fs::path&);

	void SaveVmTimingState(Framework::CZipArchiveWriter&);
	void LoadVmTimingState(Framework::CZipArchiveReader&);

	void ReloadExecutable(const char*, const CPS2OS::ArgumentList&);
	void OnExecutableChange();
	void OnCrtModeChange();

	void PauseImpl();
	void DestroyImpl();

	void CreateGsHandlerImpl(const CGSHandler::FactoryFunction&);
	void DestroyGsHandlerImpl();

	void CreatePadHandlerImpl(const CPadHandler::FactoryFunction&);
	void DestroyPadHandlerImpl();

	void CreateSoundHandlerImpl(const CSoundHandler::FactoryFunction&);
	void DestroySoundHandlerImpl();

	void ReloadSpuBlockCountImpl();

	void UpdateEe();
	void UpdateIop();
	void UpdateSpu();

	void SetIopOpticalMedia(COpticalMedia*);

	void RegisterModulesInPadHandler();

	void EmuThread();
	void WaitForNetplayFrame(uint64 frame);
	uint64 ComputeNetplayStateHash() const;

	std::thread m_thread;
	STATUS m_nStatus = PAUSED;
	bool m_nEnd = false;

	uint32 m_eeFreqScaleNumerator = 1;
	uint32 m_eeFreqScaleDenominator = 1;
	uint32 m_eeRamSize = PS2::EE_BASE_RAM_SIZE;
	uint32 m_iopRamSize = PS2::IOP_BASE_RAM_SIZE;
	uint32 m_hblankTicksTotal = 0;
	uint32 m_onScreenTicksTotal = 0;
	uint32 m_vblankTicksTotal = 0;
	int m_hblankTicks = 0;
	int m_vblankTicks = 0;
	bool m_inVblank = false;
	int64 m_spuUpdateTicks = 0;
	int64 m_spuUpdateTicksTotal = 0;
	int m_eeExecutionTicks = 0;
	int m_iopExecutionTicks = 0;
	static const int m_eeTickStep = 4800;
	int m_iopTickStep = 0;
	CFrameLimiter m_frameLimiter;

	CPU_UTILISATION_INFO m_cpuUtilisation;

	bool m_singleStepEe = false;
	bool m_singleStepIop = false;
	bool m_singleStepVu0 = false;
	bool m_singleStepVu1 = false;

	//SPU update parameters
	enum
	{
		DST_SAMPLE_RATE = 44100,
		SAMPLES_PER_UPDATE = 45, //44100 / 45 -> 980 SPU updates per second
		SPU_UPDATE_TICKS_PRECISION = 32,
		BLOCK_SIZE = SAMPLES_PER_UPDATE * 2,
		MAX_BLOCK_COUNT = 400,
	};

	int16 m_samples[BLOCK_SIZE * MAX_BLOCK_COUNT];
	int m_currentSpuBlock = 0;
	int m_spuBlockCount = 0;
	CSoundHandler* m_soundHandler = nullptr;

	CScreenPositionListener* m_gunListener = nullptr;
	CScreenPositionListener* m_touchListener = nullptr;

	CProfiler::ZoneHandle m_eeProfilerZone = 0;
	CProfiler::ZoneHandle m_iopProfilerZone = 0;
	CProfiler::ZoneHandle m_spuProfilerZone = 0;
	CProfiler::ZoneHandle m_gsSyncProfilerZone = 0;
	CProfiler::ZoneHandle m_otherProfilerZone = 0;

	CPS2OS::RequestLoadExecutableEvent::Connection m_OnRequestLoadExecutableConnection;
	Framework::CSignal<void()>::Connection m_OnExecutableChangeConnection;
	Framework::CSignal<void()>::Connection m_OnCrtModeChangeConnection;
};

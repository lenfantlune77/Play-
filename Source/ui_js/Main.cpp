#include <cstdio>
#include <vector>
#include <pthread.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <emscripten/proxying.h>
#include <emscripten/em_asm.h>
#include <emscripten/emscripten.h>
#include "Ps2VmJs.h"
#include "GSH_OpenGLJs.h"
#include "sound/SH_OpenAL/SH_OpenALProxy.h"
#include "input/PH_GenericInput.h"
#include "InputProviderEmscripten.h"
#include "ui_shared/StatsManager.h"
#include "DefaultAppConfig.h"
#include "AppConfig.h"
#include "PS2VM_Preferences.h"
#include "gs/GSHandler.h"
#include "MemoryUtils.h"
#include "MailBox.h"
#include "ee/Vpu.h"
#include "iop/IopBios.h"

CPs2VmJs* g_virtualMachine = nullptr;
//The thread that owns the canvas: main() runs there, and the machine must be built there too.
pthread_t g_mainThread = {};
CGSHandler::NewFrameEvent::Connection g_gsNewFrameConnection;
EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_context = 0;
std::shared_ptr<CInputProviderEmscripten> g_inputProvider;
CSH_OpenAL* g_soundHandler = nullptr;
static void sendEthernetFrame(const uint8*, uint32);

//A browser can refuse a WebGL context, and the failure is silent: every drawing call then runs on
//nothing. Safari on a phone refuses it once the page holds a lot of memory, so it is asked for as
//early as possible, and a second, less demanding attempt follows a refusal.
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE createGraphicsContext()
{
	EmscriptenWebGLContextAttributes attr;
	emscripten_webgl_init_context_attributes(&attr);
	attr.majorVersion = 2;
	attr.minorVersion = 0;
	attr.alpha = false;
	auto context = emscripten_webgl_create_context("#outputCanvas", &attr);
	if(context > 0) return context;

	printf("WebGL2 context refused, retrying with fewer requirements...\r\n");
	emscripten_webgl_init_context_attributes(&attr);
	attr.majorVersion = 2;
	attr.minorVersion = 0;
	attr.alpha = false;
	attr.antialias = false;
	attr.depth = false;
	attr.stencil = false;
	attr.failIfMajorPerformanceCaveat = false;
	attr.powerPreference = EM_WEBGL_POWER_PREFERENCE_LOW_POWER;
	context = emscripten_webgl_create_context("#outputCanvas", &attr);
	if(context > 0)
	{
		printf("WebGL2 context created without antialiasing.\r\n");
		return context;
	}

	printf("Failed to create a WebGL2 context: the browser refused it.\r\n");
	return 0;
}

int main(int argc, const char** argv)
{
	printf("Play! - Version %s\r\n", PLAY_VERSION);
	g_mainThread = pthread_self();
	//Claimed at load time, while the page is still light.
	g_context = createGraphicsContext();
	return 0;
}

EM_BOOL keyboardCallback(int eventType, const EmscriptenKeyboardEvent* keyEvent, void* userData)
{
	if(keyEvent->repeat)
	{
		return true;
	}
	switch(eventType)
	{
	case EMSCRIPTEN_EVENT_KEYDOWN:
		g_inputProvider->OnKeyDown(keyEvent->code);
		break;
	case EMSCRIPTEN_EVENT_KEYUP:
		g_inputProvider->OnKeyUp(keyEvent->code);
		break;
	}
	return true;
}

//Tells the page the machine is built (or could not be), so it only boots a game afterwards.
static void announceMachine(bool built)
{
	MAIN_THREAD_ASYNC_EM_ASM({
		window.dispatchEvent(new CustomEvent('ps2-vm-ready', {detail : $0 != 0}));
	}, built);
}

#ifdef PLAY_GS_ON_CANVAS_THREAD
//The thread that owns the canvas draws: on each display frame it runs the GS calls the emulation
//sends, for as long as they keep coming and within a budget, then yields so the browser shows the
//result. Waiting briefly for the next call matters: the emulation sometimes waits for the GS, and
//stopping as soon as the queue is empty would hold it until the next display frame.
//No drawing call ever crosses to another thread.
static void drainGs()
{
	if(!g_virtualMachine) return;
	auto gs = g_virtualMachine->GetGSHandler();
	if(!gs) return;
	const double deadline = emscripten_get_now() + 12.0;
	while(gs->ProcessPendingCall(2))
	{
		if(emscripten_get_now() > deadline) break;
	}
}
#endif

static void buildMachine(void*)
{
	if(g_context <= 0)
	{
		g_context = createGraphicsContext();
	}
	if(g_context <= 0)
	{
		//Going further would only produce a stream of failed drawing calls.
		announceMachine(false);
		return;
	}

	g_virtualMachine = new CPs2VmJs();
	g_virtualMachine->Initialize();
#ifdef PLAY_GS_ON_CANVAS_THREAD
	g_virtualMachine->CreateGSHandler(CGSH_OpenGLJs::GetFactoryFunction(g_context, false));
	emscripten_set_main_loop(drainGs, 0, false);
#else
	g_virtualMachine->CreateGSHandler(CGSH_OpenGLJs::GetFactoryFunction(g_context));
#endif

	{
		//Size here needs to match the size of the canvas in HTML file.

		CGSHandler::PRESENTATION_PARAMS presentationParams;
		presentationParams.mode = CGSHandler::PRESENTATION_MODE_FIT;
		presentationParams.windowWidth = 640;
		presentationParams.windowHeight = 480;

		g_virtualMachine->m_ee->m_gs->SetPresentationParams(presentationParams);
	}

	{
		g_virtualMachine->CreatePadHandler(CPH_GenericInput::GetFactoryFunction());
		auto padHandler = static_cast<CPH_GenericInput*>(g_virtualMachine->GetPadHandler());
		auto& bindingManager = padHandler->GetBindingManager();

		g_inputProvider = std::make_shared<CInputProviderEmscripten>();
		bindingManager.RegisterInputProvider(g_inputProvider);
		g_virtualMachine->SetNetplayInputApplyHandler([](uint64 frame) {
			if(g_inputProvider) g_inputProvider->ApplyFrame(frame);
		});
		g_virtualMachine->SetNetplayInputReplayHandler([](uint64 frame) {
			if(g_inputProvider) g_inputProvider->ReplayFrame(frame);
		});
		g_inputProvider->SetLiveFrameProvider([]() -> uint64 {
			return g_virtualMachine ? g_virtualMachine->GetNetplayFrame() : 0;
		});

		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::START, CInputProviderEmscripten::MakeBindingTarget("Enter"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::SELECT, CInputProviderEmscripten::MakeBindingTarget("Backspace"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::DPAD_LEFT, CInputProviderEmscripten::MakeBindingTarget("ArrowLeft"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::DPAD_RIGHT, CInputProviderEmscripten::MakeBindingTarget("ArrowRight"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::DPAD_UP, CInputProviderEmscripten::MakeBindingTarget("ArrowUp"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::DPAD_DOWN, CInputProviderEmscripten::MakeBindingTarget("ArrowDown"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::SQUARE, CInputProviderEmscripten::MakeBindingTarget("KeyA"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::CROSS, CInputProviderEmscripten::MakeBindingTarget("KeyZ"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::TRIANGLE, CInputProviderEmscripten::MakeBindingTarget("KeyS"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::CIRCLE, CInputProviderEmscripten::MakeBindingTarget("KeyX"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::L1, CInputProviderEmscripten::MakeBindingTarget("Key1"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::L2, CInputProviderEmscripten::MakeBindingTarget("Key2"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::L3, CInputProviderEmscripten::MakeBindingTarget("Key3"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::R1, CInputProviderEmscripten::MakeBindingTarget("Key8"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::R2, CInputProviderEmscripten::MakeBindingTarget("Key9"));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::R3, CInputProviderEmscripten::MakeBindingTarget("Key0"));

		//True analog sticks: the page sends a value from 0x00 to 0xFF per axis, 0x7F at rest.
		//It feeds them from a thumbstick on screen, from a gamepad, or from the keyboard.
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::ANALOG_LEFT_X, CInputProviderEmscripten::MakeAxisTarget(0));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::ANALOG_LEFT_Y, CInputProviderEmscripten::MakeAxisTarget(1));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::ANALOG_RIGHT_X, CInputProviderEmscripten::MakeAxisTarget(2));
		bindingManager.SetSimpleBinding(0, PS2::CControllerInfo::ANALOG_RIGHT_Y, CInputProviderEmscripten::MakeAxisTarget(3));

		// The on-screen pad and keyboard become the second player while a physical gamepad is active.
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::START, CInputProviderEmscripten::MakeBindingTarget("Pad2Start"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::SELECT, CInputProviderEmscripten::MakeBindingTarget("Pad2Select"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::DPAD_LEFT, CInputProviderEmscripten::MakeBindingTarget("Pad2Left"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::DPAD_RIGHT, CInputProviderEmscripten::MakeBindingTarget("Pad2Right"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::DPAD_UP, CInputProviderEmscripten::MakeBindingTarget("Pad2Up"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::DPAD_DOWN, CInputProviderEmscripten::MakeBindingTarget("Pad2Down"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::SQUARE, CInputProviderEmscripten::MakeBindingTarget("Pad2Square"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::CROSS, CInputProviderEmscripten::MakeBindingTarget("Pad2Cross"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::TRIANGLE, CInputProviderEmscripten::MakeBindingTarget("Pad2Triangle"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::CIRCLE, CInputProviderEmscripten::MakeBindingTarget("Pad2Circle"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::L1, CInputProviderEmscripten::MakeBindingTarget("Pad2L1"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::L2, CInputProviderEmscripten::MakeBindingTarget("Pad2L2"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::L3, CInputProviderEmscripten::MakeBindingTarget("Pad2L3"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::R1, CInputProviderEmscripten::MakeBindingTarget("Pad2R1"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::R2, CInputProviderEmscripten::MakeBindingTarget("Pad2R2"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::R3, CInputProviderEmscripten::MakeBindingTarget("Pad2R3"));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::ANALOG_LEFT_X, CInputProviderEmscripten::MakeAxisTarget(4));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::ANALOG_LEFT_Y, CInputProviderEmscripten::MakeAxisTarget(5));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::ANALOG_RIGHT_X, CInputProviderEmscripten::MakeAxisTarget(6));
		bindingManager.SetSimpleBinding(1, PS2::CControllerInfo::ANALOG_RIGHT_Y, CInputProviderEmscripten::MakeAxisTarget(7));
	}

	{
		g_soundHandler = new CSH_OpenAL();
		g_virtualMachine->CreateSoundHandler(CSH_OpenALProxy::GetFactoryFunction(g_soundHandler));
	}

	g_gsNewFrameConnection = g_virtualMachine->GetGSHandler()->OnNewFrame.Connect(std::bind(&CStatsManager::OnGsNewFrame, &CStatsManager::GetInstance(), std::placeholders::_1));
	g_virtualMachine->m_iop->m_speed.SetEthernetFrameTxHandler(&sendEthernetFrame);

	EMSCRIPTEN_RESULT result = EMSCRIPTEN_RESULT_SUCCESS;

	result = emscripten_set_keydown_callback("#outputCanvas", nullptr, false, &keyboardCallback);
	assert(result == EMSCRIPTEN_RESULT_SUCCESS);

	result = emscripten_set_keyup_callback("#outputCanvas", nullptr, false, &keyboardCallback);
	assert(result == EMSCRIPTEN_RESULT_SUCCESS);

	announceMachine(true);
}

extern "C" void initVm()
{
	//The page calls this from the browser's main thread. When the canvas belongs to another thread,
	//the machine has to be built there, otherwise the graphics context would not be usable.
	if(pthread_equal(pthread_self(), g_mainThread))
	{
		buildMachine(nullptr);
		return;
	}
	//Never wait here: building the machine sends calls back to this very thread (keyboard
	//callbacks, audio), which would then never run. The page waits for 'ps2-vm-ready' instead.
	emscripten_proxy_async(emscripten_proxy_get_system_queue(), g_mainThread, buildMachine, nullptr);
}

void setPadButtons(int pad, int mask)
{
	if(!g_inputProvider) return;
	g_inputProvider->SetPadButtons(static_cast<uint32>(pad), static_cast<uint32>(mask));
}

void setAxis(int axis, int value)
{
	if(!g_inputProvider) return;
	g_inputProvider->OnAxis(static_cast<uint32>(axis), static_cast<uint32>(value));
}

void queueNetplayAxis(uint64 frame, uint32 axis, uint32 value)
{
	if(g_inputProvider) g_inputProvider->QueueAxis(frame, axis, value);
}

void queueNetplayButton(uint64 frame, uint32 keyCode, uint32 value)
{
	if(g_inputProvider) g_inputProvider->QueueButton(frame, keyCode, value);
}

void queueNetplayButtonCode(uint64 frame, std::string code, uint32 value)
{
	if(g_inputProvider) g_inputProvider->QueueButtonCode(frame, code.c_str(), value);
}

void setNetplayLockstep(bool enabled)
{
	if(g_virtualMachine) g_virtualMachine->SetNetplayLockstep(enabled);
}

void resetNetplayFrame()
{
	if(g_virtualMachine) g_virtualMachine->ResetNetplayFrame();
}

uint64 getNetplayFrame()
{
	return g_virtualMachine ? g_virtualMachine->GetNetplayFrame() : 0;
}

void advanceNetplayFrame(uint64 frame)
{
	if(g_virtualMachine) g_virtualMachine->AdvanceNetplayFrame(frame);
}

uint64 getNetplayStateHash()
{
	return g_virtualMachine ? g_virtualMachine->GetNetplayStateHash() : 0;
}

bool saveNetplayState(std::string path)
{
	return g_virtualMachine && g_virtualMachine->SaveNetplayState(path);
}

bool loadNetplayState(std::string path)
{
	return g_virtualMachine && g_virtualMachine->LoadNetplayState(path);
}

bool captureNetplaySnapshot()
{
	return g_virtualMachine && g_virtualMachine->CaptureNetplaySnapshot();
}

bool restoreNetplaySnapshot()
{
	return g_virtualMachine && g_virtualMachine->RestoreNetplaySnapshot();
}

uint64 getNetplaySnapshotFrame()
{
	return g_virtualMachine ? g_virtualMachine->GetNetplaySnapshotFrame() : 0;
}

emscripten::val getNetplaySnapshot()
{
	if(!g_virtualMachine) return emscripten::val::null();
	auto bytes = g_virtualMachine->GetNetplaySnapshot();
	auto result = emscripten::val::global("Uint8Array").new_(bytes.size());
	for(size_t i = 0; i < bytes.size(); i++) result.call<void>("set", i, bytes[i]);
	return result;
}

void setNetplaySnapshot(emscripten::val packet, uint64 frame)
{
	if(!g_virtualMachine) return;
	const auto size = packet["length"].as<uint32>();
	if(size == 0 || size > 128 * 1024 * 1024) return;
	std::vector<uint8> bytes(size);
	for(uint32 i = 0; i < size; i++) bytes[i] = packet[i].as<uint8>();
	g_virtualMachine->SetNetplaySnapshot(bytes, frame);
}

bool rollbackNetplayFrame(uint64 frame)
{
	return g_virtualMachine && g_virtualMachine->RollbackNetplayFrame(frame);
}

void replayNetplayFrames(uint64 targetFrame)
{
	if(g_virtualMachine) g_virtualMachine->ReplayNetplayFrames(targetFrame);
}

// Ethernet frames emitted by the emulated SMAP are handed to the browser transport. The
// transport is intentionally a tiny binary boundary: the PS2 networking stack remains inside
// Play!, while the browser only carries complete Ethernet frames.
static void sendEthernetFrame(const uint8* data, uint32 size)
{
	using namespace emscripten;
	auto module = val::global("Module");
	auto net = module["ps2Net"];
	if(net.isUndefined() || net.isNull()) return;
	auto packet = val::global("Uint8Array").new_(size);
	for(uint32 i = 0; i < size; i++) packet.call<void>("set", i, data[i]);
	net.call<void>("send", packet);
}

void receiveEthernetFrame(emscripten::val packet)
{
	if(!g_virtualMachine || !g_virtualMachine->m_iop) return;
	const auto size = packet["length"].as<uint32>();
	if(size == 0 || size > 0x2000) return;
	std::vector<uint8> frame(size);
	for(uint32 i = 0; i < size; i++) frame[i] = packet[i].as<uint8>();
	g_virtualMachine->m_iop->m_speed.RxEthernetFrame(frame.data(), size);
}

//Three knobs the page can turn when a game runs too slowly. They only make sense once the machine
//exists, since that is when these preferences are registered.
void setRamReads(bool enabled)
{
	if(!g_virtualMachine) return;
	CAppConfig::GetInstance().SetPreferenceBoolean(PREF_CGSHANDLER_GS_RAM_READS_ENABLED, enabled);
	if(auto gs = g_virtualMachine->GetGSHandler())
	{
		gs->NotifyPreferencesChanged();
	}
}

void setFrameLimit(bool enabled)
{
	if(!g_virtualMachine) return;
	CAppConfig::GetInstance().SetPreferenceBoolean(PREF_PS2_LIMIT_FRAMERATE, enabled);
	g_virtualMachine->ReloadFrameRateLimit();
}

void setEeFrequencyScale(int numerator, int denominator)
{
	if(!g_virtualMachine) return;
	if((numerator <= 0) || (denominator <= 0)) return;
	g_virtualMachine->SetEeFrequencyScale(static_cast<uint32>(numerator), static_cast<uint32>(denominator));
}

void bootElf(std::string path)
{
	g_virtualMachine->BootElf(path);
}

void bootDiscImage(std::string path)
{
	g_virtualMachine->BootDiscImage(path);
}

//Where each processor of the console stands, for the page to tell, when a game stops drawing,
//a processor looping on a wait from a machine that no longer runs at all.
std::string getCpuState()
{
	if(!g_virtualMachine) return "machine absente";
	static const char* steps[] = {"boucle", "messages", "son", "debut d'image", "fin d'image", "EE", "IOP"};
	uint32 step = g_virtualMachine->m_emuStep;
	uint32 pc = g_virtualMachine->m_ee->m_EE.m_State.nPC;
	char state[4096];
	int length = snprintf(state, sizeof(state), "EE 0x%08X, IOP 0x%08X, machine %s, %u tours, etape %s",
	                      pc,
	                      g_virtualMachine->m_iop->m_cpu.m_State.nPC,
	                      (g_virtualMachine->GetStatus() == CVirtualMachine::RUNNING) ? "en marche" : "en pause",
	                      static_cast<uint32>(g_virtualMachine->m_emuLoops),
	                      (step < 7) ? steps[step] : "?");
	//The registers the code below reads its addresses from (t0, s4, s5, gp, sp, ra).
	const auto& gpr = g_virtualMachine->m_ee->m_EE.m_State.nGPR;
	length += snprintf(state + length, sizeof(state) - length, " ; t0 %08X s4 %08X s5 %08X gp %08X sp %08X ra %08X ;",
	                   gpr[CMIPS::T0].nV0, gpr[CMIPS::S4].nV0, gpr[CMIPS::S5].nV0,
	                   gpr[CMIPS::GP].nV0, gpr[CMIPS::SP].nV0, gpr[CMIPS::RA].nV0);
	//Values a loop would change: if these move between two readings, compiled code is spinning.
	const auto& eeState = g_virtualMachine->m_ee->m_EE.m_State;
	length += snprintf(state + length, sizeof(state) - length,
	                   " quota %d exception %u ; a0 %08X a1 %08X a2 %08X a3 %08X v0 %08X v1 %08X t4 %08X t5 %08X ;",
	                   eeState.cycleQuota, eeState.nHasException,
	                   gpr[CMIPS::A0].nV0, gpr[CMIPS::A1].nV0, gpr[CMIPS::A2].nV0, gpr[CMIPS::A3].nV0,
	                   gpr[CMIPS::V0].nV0, gpr[CMIPS::V1].nV0, gpr[CMIPS::T4].nV0, gpr[CMIPS::T5].nV0);
	//The EE waits for a VU0 microprogram while callMs is set: say where VU0 is and whether it runs.
	{
		static const char* vuStates[] = {"pret", "en marche", "arrete"};
		auto vu0 = g_virtualMachine->m_ee->m_vpu0;
		uint32 vuState = vu0->GetVuState();
		length += snprintf(state + length, sizeof(state) - length,
		                   " callMs %u adresse %04X ; VU0 %s pc %04X exception %u ;",
		                   eeState.callMsEnabled, eeState.callMsAddr,
		                   (vuState < 3) ? vuStates[vuState] : "?",
		                   g_virtualMachine->m_ee->m_VU0.m_State.nPC,
		                   g_virtualMachine->m_ee->m_VU0.m_State.nHasException);
		//What a VU0 microprogram that never ends is doing: its next instructions (pairs of 32-bit
		//words, lower then upper) and its integer registers, which hold its loop counters and addresses.
		const auto& vu0State = g_virtualMachine->m_ee->m_VU0.m_State;
		length += snprintf(state + length, sizeof(state) - length, " VI");
		for(uint32 i = 0; i < 16; i++)
		{
			length += snprintf(state + length, sizeof(state) - length, " %04X", vu0State.nCOP2VI[i] & 0xFFFF);
		}
		//The EE's own copy of the same registers: a value written by the EE that never reached VU0
		//shows up here and not above.
		length += snprintf(state + length, sizeof(state) - length, " ; VIee");
		for(uint32 i = 0; i < 16; i++)
		{
			length += snprintf(state + length, sizeof(state) - length, " %04X", eeState.nCOP2VI[i] & 0xFFFF);
		}
		length += snprintf(state + length, sizeof(state) - length, " ; microcode");
		const uint8* micro = vu0->GetMicroMemory();
		uint32 microSize = vu0->GetMicroMemorySize();
		uint32 vuPc = vu0State.nPC & ~7U;
		for(uint32 i = 0; (i < 16) && (vuPc + i * 8 + 8 <= microSize); i++)
		{
			const uint32* pair = reinterpret_cast<const uint32*>(micro + vuPc + i * 8);
			length += snprintf(state + length, sizeof(state) - length, " %08X:%08X", pair[0], pair[1]);
		}
		length += snprintf(state + length, sizeof(state) - length, " ;");
	}
	//The EE often waits for a DMA to VU0 (channel 0, VIF0), which waits for VU0 in turn: what the
	//channel still has to send, what VIF0 is doing, and whether VU0 runs alongside the EE.
	{
		auto& map = *g_virtualMachine->m_ee->m_EE.m_pMemoryMap;
		length += snprintf(state + length, sizeof(state) - length,
		                   " async %u ; D0 chcr %08X madr %08X qwc %08X tadr %08X ; VIF0 stat %08X code %08X num %08X ;",
		                   eeState.vu0Async,
		                   map.GetWord(0x10008000), map.GetWord(0x10008010), map.GetWord(0x10008020), map.GetWord(0x10008030),
		                   map.GetWord(0x10003800), map.GetWord(0x10003880), map.GetWord(0x10003860));
	}
	//A game that keeps running while a movie or a sound never ends usually waits for the IOP or a
	//device: the video decoder (IPU and its DMA channels 3 and 4), interrupts, the sound (SPU2 and
	//its IOP DMA channels 4 and 7, IRQ address), the disc (IOP DMA channel 3), and each IOP thread.
	{
		auto& ee = *g_virtualMachine->m_ee->m_EE.m_pMemoryMap;
		auto& iop = *g_virtualMachine->m_iop->m_cpu.m_pMemoryMap;
		length += snprintf(state + length, sizeof(state) - length,
		                   " IPU ctrl %08X bp %08X d3 %08X/%04X d4 %08X/%04X ; DMAC stat %08X INTC %08X/%08X ;",
		                   ee.GetWord(0x10002010), ee.GetWord(0x10002020),
		                   ee.GetWord(0x1000B000), ee.GetWord(0x1000B020) & 0xFFFF,
		                   ee.GetWord(0x1000B400), ee.GetWord(0x1000B420) & 0xFFFF,
		                   ee.GetWord(0x1000E010), ee.GetWord(0x1000F000), ee.GetWord(0x1000F010));
		length += snprintf(state + length, sizeof(state) - length,
		                   " IOP intc %08X/%08X dma3 %08X/%08X dma4 %08X/%08X dma7 %08X/%08X ; SPU attr %04X/%04X irqa %04X%04X/%04X%04X ;",
		                   iop.GetWord(0x1F801070), iop.GetWord(0x1F801074),
		                   iop.GetWord(0x1F8010B8), iop.GetWord(0x1F8010B4),
		                   iop.GetWord(0x1F8010C8), iop.GetWord(0x1F8010C4),
		                   iop.GetWord(0x1F801508), iop.GetWord(0x1F801504),
		                   iop.GetHalf(0x1F90019A), iop.GetHalf(0x1F90059A),
		                   iop.GetHalf(0x1F90019C), iop.GetHalf(0x1F90019E), iop.GetHalf(0x1F90059C), iop.GetHalf(0x1F90059E));
		auto bios = std::dynamic_pointer_cast<CIopBios>(g_virtualMachine->m_iop->m_bios);
		if(bios)
		{
			std::string threads = bios->GetThreadsSummary();
			length += snprintf(state + length, sizeof(state) - length, " IOPth%s ;", threads.c_str());
		}
	}
	//What compiled code is calling out to, and how many threads wait on one another.
	uint32 access = g_memoryProxyAccess;
	if(access != 0)
	{
		length += snprintf(state + length, sizeof(state) - length, " acces %s 0x%08X ;", (access & 1) ? "en ecriture" : "en lecture", access & ~1U);
	}
	else
	{
		length += snprintf(state + length, sizeof(state) - length, " aucun acces en cours ;");
	}
	length += snprintf(state + length, sizeof(state) - length, " %d attentes entre fils ;", static_cast<int>(CMailBox::g_waitingSenders));
	//The instructions where the EE stands, to tell what a game waits for. Main RAM is 32 MB.
	length += snprintf(state + length, sizeof(state) - length, " code");
	uint32 address = pc & 0x01FFFFFC;
	for(uint32 i = 0; (i < 32) && (address + i * 4 + 4 <= 0x02000000) && (length < static_cast<int>(sizeof(state)) - 10); i++)
	{
		uint32 word = *reinterpret_cast<uint32*>(g_virtualMachine->m_ee->m_ram + address + i * 4);
		length += snprintf(state + length, sizeof(state) - length, " %08X", word);
	}
	return state;
}

int getFrames()
{
	return CStatsManager::GetInstance().GetFrames();
}

void clearStats()
{
	CStatsManager::GetInstance().ClearStats();
}

EMSCRIPTEN_BINDINGS(Play)
{
	using namespace emscripten;

	function("setAxis", &setAxis);
	function("setPadButtons", &setPadButtons);
	function("queueNetplayAxis", &queueNetplayAxis);
	function("queueNetplayButton", &queueNetplayButton);
	function("queueNetplayButtonCode", &queueNetplayButtonCode);
	function("setNetplayLockstep", &setNetplayLockstep);
	function("resetNetplayFrame", &resetNetplayFrame);
	function("getNetplayFrame", &getNetplayFrame);
	function("advanceNetplayFrame", &advanceNetplayFrame);
	function("getNetplayStateHash", &getNetplayStateHash);
	function("saveNetplayState", &saveNetplayState);
	function("loadNetplayState", &loadNetplayState);
	function("captureNetplaySnapshot", &captureNetplaySnapshot);
	function("restoreNetplaySnapshot", &restoreNetplaySnapshot);
	function("getNetplaySnapshotFrame", &getNetplaySnapshotFrame);
	function("getNetplaySnapshot", &getNetplaySnapshot);
	function("setNetplaySnapshot", &setNetplaySnapshot);
	function("rollbackNetplayFrame", &rollbackNetplayFrame);
	function("replayNetplayFrames", &replayNetplayFrames);
	function("receiveEthernetFrame", &receiveEthernetFrame);
	function("setRamReads", &setRamReads);
	function("setFrameLimit", &setFrameLimit);
	function("setEeFrequencyScale", &setEeFrequencyScale);
	function("bootElf", &bootElf);
	function("bootDiscImage", &bootDiscImage);
	function("getFrames", &getFrames);
	function("getCpuState", &getCpuState);
	function("clearStats", &clearStats);
}

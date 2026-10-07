#include "InputProviderEmscripten.h"
#include "string_format.h"
#include <cassert>
#include <iterator>

constexpr uint32 PROVIDER_ID = 'EmSc';

enum
{
	INPUT_ARROW_UP = 0xFF80,
	INPUT_ARROW_DOWN,
	INPUT_ARROW_LEFT,
	INPUT_ARROW_RIGHT,
	INPUT_ENTER,
	INPUT_BACKSPACE,
	INPUT_SHIFT_RIGHT,

	INPUT_KEY_A,
	INPUT_KEY_F,
	INPUT_KEY_G,
	INPUT_KEY_H,
	INPUT_KEY_I,
	INPUT_KEY_J,
	INPUT_KEY_K,
	INPUT_KEY_L,
	INPUT_KEY_S,
	INPUT_KEY_T,
	INPUT_KEY_X,
	INPUT_KEY_Z,

	INPUT_KEY_0,
	INPUT_KEY_1,
	INPUT_KEY_2,
	INPUT_KEY_3,
	INPUT_KEY_8,
	INPUT_KEY_9,
};

//Analog axes, driven from JavaScript: left X, left Y, right X, right Y.
enum
{
	INPUT_AXIS_BASE = 0xFE00,
	INPUT_AXIS_COUNT = 8,
	INPUT_PAD2_BASE = 0xFD00,
};

uint32 CInputProviderEmscripten::GetId() const
{
	return PROVIDER_ID;
}

std::string CInputProviderEmscripten::GetTargetDescription(const BINDINGTARGET& target) const
{
	return std::string();
}

BINDINGTARGET CInputProviderEmscripten::MakeBindingTarget(const EM_UTF8* code)
{
	uint32 keyCode = 0;
	if(!strcmp(code, "ArrowUp"))
		keyCode = INPUT_ARROW_UP;
	else if(!strcmp(code, "ArrowDown"))
		keyCode = INPUT_ARROW_DOWN;
	else if(!strcmp(code, "ArrowLeft"))
		keyCode = INPUT_ARROW_LEFT;
	else if(!strcmp(code, "ArrowRight"))
		keyCode = INPUT_ARROW_RIGHT;
	else if(!strcmp(code, "Enter"))
		keyCode = INPUT_ENTER;
	else if(!strcmp(code, "ShiftRight"))
		keyCode = INPUT_SHIFT_RIGHT;
	else if(!strcmp(code, "Backspace"))
		keyCode = INPUT_BACKSPACE;
	else if(!strcmp(code, "KeyA"))
		keyCode = INPUT_KEY_A;
	else if(!strcmp(code, "KeyF"))
		keyCode = INPUT_KEY_F;
	else if(!strcmp(code, "KeyG"))
		keyCode = INPUT_KEY_G;
	else if(!strcmp(code, "KeyH"))
		keyCode = INPUT_KEY_H;
	else if(!strcmp(code, "KeyI"))
		keyCode = INPUT_KEY_I;
	else if(!strcmp(code, "KeyJ"))
		keyCode = INPUT_KEY_J;
	else if(!strcmp(code, "KeyK"))
		keyCode = INPUT_KEY_K;
	else if(!strcmp(code, "KeyL"))
		keyCode = INPUT_KEY_L;
	else if(!strcmp(code, "KeyS"))
		keyCode = INPUT_KEY_S;
	else if(!strcmp(code, "KeyT"))
		keyCode = INPUT_KEY_T;
	else if(!strcmp(code, "KeyX"))
		keyCode = INPUT_KEY_X;
	else if(!strcmp(code, "KeyZ"))
		keyCode = INPUT_KEY_Z;
	else if(!strcmp(code, "Key0"))
		keyCode = INPUT_KEY_0;
	else if(!strcmp(code, "Key1"))
		keyCode = INPUT_KEY_1;
	else if(!strcmp(code, "Key2"))
		keyCode = INPUT_KEY_2;
	else if(!strcmp(code, "Key3"))
		keyCode = INPUT_KEY_3;
	else if(!strcmp(code, "Key8"))
		keyCode = INPUT_KEY_8;
	else if(!strcmp(code, "Key9"))
		keyCode = INPUT_KEY_9;
	else if(!strcmp(code, "Pad2Up"))
		keyCode = INPUT_PAD2_BASE + 0;
	else if(!strcmp(code, "Pad2Down"))
		keyCode = INPUT_PAD2_BASE + 1;
	else if(!strcmp(code, "Pad2Left"))
		keyCode = INPUT_PAD2_BASE + 2;
	else if(!strcmp(code, "Pad2Right"))
		keyCode = INPUT_PAD2_BASE + 3;
	else if(!strcmp(code, "Pad2Select"))
		keyCode = INPUT_PAD2_BASE + 4;
	else if(!strcmp(code, "Pad2Start"))
		keyCode = INPUT_PAD2_BASE + 5;
	else if(!strcmp(code, "Pad2Square"))
		keyCode = INPUT_PAD2_BASE + 6;
	else if(!strcmp(code, "Pad2Triangle"))
		keyCode = INPUT_PAD2_BASE + 7;
	else if(!strcmp(code, "Pad2Circle"))
		keyCode = INPUT_PAD2_BASE + 8;
	else if(!strcmp(code, "Pad2Cross"))
		keyCode = INPUT_PAD2_BASE + 9;
	else if(!strcmp(code, "Pad2L1"))
		keyCode = INPUT_PAD2_BASE + 10;
	else if(!strcmp(code, "Pad2L2"))
		keyCode = INPUT_PAD2_BASE + 11;
	else if(!strcmp(code, "Pad2L3"))
		keyCode = INPUT_PAD2_BASE + 12;
	else if(!strcmp(code, "Pad2R1"))
		keyCode = INPUT_PAD2_BASE + 13;
	else if(!strcmp(code, "Pad2R2"))
		keyCode = INPUT_PAD2_BASE + 14;
	else if(!strcmp(code, "Pad2R3"))
		keyCode = INPUT_PAD2_BASE + 15;
	else
		keyCode = code[0];
	return BINDINGTARGET(PROVIDER_ID, DeviceIdType{{0}}, keyCode, BINDINGTARGET::KEYTYPE::BUTTON);
}

BINDINGTARGET CInputProviderEmscripten::MakeAxisTarget(uint32 axis)
{
	assert(axis < INPUT_AXIS_COUNT);
	return BINDINGTARGET(PROVIDER_ID, DeviceIdType{{0}}, INPUT_AXIS_BASE + axis, BINDINGTARGET::KEYTYPE::AXIS);
}

void CInputProviderEmscripten::OnAxis(uint32 axis, uint32 value)
{
	if(axis >= INPUT_AXIS_COUNT) return;
	if(value > BINDINGTARGET::AXIS_MAX) value = BINDINGTARGET::AXIS_MAX;
	RecordLiveInput(INPUT_AXIS_BASE + axis, value, true);
	OnInput(MakeAxisTarget(axis), value);
}

BINDINGTARGET CInputProviderEmscripten::MakePadButtonTarget(uint32 pad, uint32 button)
{
	assert(pad < PAD_COUNT);
	assert(button >= 4 && button < 20);
	// Keep whole-pad button updates on the same key IDs as the Pad2 bindings below.
	return BINDINGTARGET(PROVIDER_ID, DeviceIdType{{0}}, INPUT_PAD2_BASE + (button - 4), BINDINGTARGET::KEYTYPE::BUTTON);
}

void CInputProviderEmscripten::SetPadButtons(uint32 pad, uint32 mask)
{
	if(pad >= PAD_COUNT) return;
	uint32 changed = m_padButtons[pad] ^ mask;
	m_padButtons[pad] = mask;
	for(uint32 button = 4; button < 20; button++)
	{
		if(changed & (1U << button))
		{
			OnInput(MakePadButtonTarget(pad, button), (mask >> button) & 1);
		}
	}
}

void CInputProviderEmscripten::OnKeyDown(const EM_UTF8* code)
{
	const auto target = MakeBindingTarget(code);
	RecordLiveInput(target.keyId, 1, false);
	OnInput(target, 1);
}

void CInputProviderEmscripten::OnKeyUp(const EM_UTF8* code)
{
	const auto target = MakeBindingTarget(code);
	RecordLiveInput(target.keyId, 0, false);
	OnInput(target, 0);
}

void CInputProviderEmscripten::SetLiveFrameProvider(const std::function<uint64()>& provider)
{
	m_liveFrameProvider = provider;
}

void CInputProviderEmscripten::RecordLiveInput(uint32 keyCode, uint32 value, bool axis)
{
	if(!m_liveFrameProvider) return;
	const auto frame = m_liveFrameProvider() + 1;
	std::lock_guard<std::mutex> lock(m_queuedInputsMutex);
	m_inputHistory[frame].push_back({ keyCode, value, axis });
	while(m_inputHistory.size() > 600) m_inputHistory.erase(m_inputHistory.begin());
}

void CInputProviderEmscripten::QueueAxis(uint64 frame, uint32 axis, uint32 value)
{
	if(axis >= INPUT_AXIS_COUNT) return;
	std::lock_guard<std::mutex> lock(m_queuedInputsMutex);
	m_queuedInputs[frame].push_back({ INPUT_AXIS_BASE + axis, value, true });
}

void CInputProviderEmscripten::QueueButton(uint64 frame, uint32 keyCode, uint32 value)
{
	std::lock_guard<std::mutex> lock(m_queuedInputsMutex);
	m_queuedInputs[frame].push_back({ keyCode, value ? 1U : 0U, false });
}

void CInputProviderEmscripten::QueueButtonCode(uint64 frame, const EM_UTF8* code, uint32 value)
{
	const auto target = MakeBindingTarget(code);
	QueueButton(frame, target.keyId, value);
}

void CInputProviderEmscripten::ApplyFrame(uint64 frame)
{
	std::vector<QueuedInput> inputs;
	{
		std::lock_guard<std::mutex> lock(m_queuedInputsMutex);
		auto found = m_queuedInputs.find(frame);
		if(found == m_queuedInputs.end()) return;
		inputs = std::move(found->second);
		m_inputHistory[frame] = inputs;
		while(m_inputHistory.size() > 600) m_inputHistory.erase(m_inputHistory.begin());
		// Consume the frame as well as all older frames. Keeping the current entry
		// here made a held remote button get replayed forever.
		m_queuedInputs.erase(m_queuedInputs.begin(), std::next(found));
	}
	for(const auto& input : inputs)
	{
		const auto target = BINDINGTARGET(PROVIDER_ID, DeviceIdType{{0}}, input.keyCode,
		                                 input.axis ? BINDINGTARGET::KEYTYPE::AXIS : BINDINGTARGET::KEYTYPE::BUTTON);
		OnInput(target, input.value);
	}
}

void CInputProviderEmscripten::ReplayFrame(uint64 frame)
{
	std::vector<QueuedInput> inputs;
	{
		std::lock_guard<std::mutex> lock(m_queuedInputsMutex);
		auto found = m_inputHistory.find(frame);
		if(found == m_inputHistory.end()) return;
		inputs = found->second;
	}
	for(const auto& input : inputs)
	{
		const auto target = BINDINGTARGET(PROVIDER_ID, DeviceIdType{{0}}, input.keyCode,
		                                 input.axis ? BINDINGTARGET::KEYTYPE::AXIS : BINDINGTARGET::KEYTYPE::BUTTON);
		OnInput(target, input.value);
	}
}

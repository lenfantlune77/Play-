#pragma once

#include "input/InputProvider.h"
#include <emscripten/html5.h>
#include <map>
#include <mutex>
#include <vector>

class CInputProviderEmscripten : public CInputProvider
{
public:
	uint32 GetId() const override;
	std::string GetTargetDescription(const BINDINGTARGET&) const override;

	static BINDINGTARGET MakeBindingTarget(const EM_UTF8* code);
	//Analog axis, 0x00 to 0xFF with 0x7F at rest: a thumbstick, on screen or on a gamepad.
	static BINDINGTARGET MakeAxisTarget(uint32 axis);
	//A button of a given pad, set by SetPadButtons. The second pad is driven this way: a whole pad
	//state at once, which is what a remote player will send over the network.
	static BINDINGTARGET MakePadButtonTarget(uint32 pad, uint32 button);

	void OnKeyDown(const EM_UTF8*);
	void OnKeyUp(const EM_UTF8*);
	void OnAxis(uint32 axis, uint32 value);
	//One bit per PS2::CControllerInfo::BUTTON; only the buttons that changed are reported.
	void SetPadButtons(uint32 pad, uint32 mask);
	void QueueAxis(uint64 frame, uint32 axis, uint32 value);
	void QueueButton(uint64 frame, uint32 keyCode, uint32 value);
	void QueueButtonCode(uint64 frame, const EM_UTF8* code, uint32 value);
	void ApplyFrame(uint64 frame);
	void ReplayFrame(uint64 frame);
	void SetLiveFrameProvider(const std::function<uint64()>& provider);

private:
	enum
	{
		PAD_COUNT = 4,
	};
	uint32 m_padButtons[PAD_COUNT] = {};

private:
	struct QueuedInput { uint32 keyCode; uint32 value; bool axis; };
	std::mutex m_queuedInputsMutex;
	std::map<uint64, std::vector<QueuedInput>> m_queuedInputs;
	std::map<uint64, std::vector<QueuedInput>> m_inputHistory;
	std::function<uint64()> m_liveFrameProvider;
	void RecordLiveInput(uint32 keyCode, uint32 value, bool axis);
};

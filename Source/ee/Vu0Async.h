#pragma once

#include "../MIPS.h"

//While a VU0 microprogram started by VCALLMS runs alongside the EE, as it does on the console, the
//EE and VU0 keep their own copies of the VU0 registers. Compiled EE code calls these around the
//COP2 transfers that do not wait for VU0, so that the EE reads and writes the registers VU0 uses.
extern "C"
{
	//Copies the VU0 registers into the EE copy.
	void Vu0Async_Pull(CMIPS* ee);
	//Copies the EE copy of the VU0 registers into VU0.
	void Vu0Async_Push(CMIPS* ee);
	//The EE reached an instruction that waits for VU0: runs the microprogram to its end, as the
	//console holds the EE meanwhile. Its registers come back to the EE when it ends.
	void Vu0Async_Wait(CMIPS* ee);
	//The EE just wrote a VU0 register with CTC2: lets VU0 react at once, as it runs alongside the
	//EE and much faster on the console. Games hand work to VU0 this way without checking that it
	//took the previous item (Rayman M writes VI6 once per item).
	void Vu0Async_Signal(CMIPS* ee);
}

class CVpu;

//The VU0 context and unit the functions above work with.
void Vu0Async_SetContext(CMIPS* vu0, CVpu* vpu0);

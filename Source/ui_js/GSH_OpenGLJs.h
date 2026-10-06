#pragma once

#include <emscripten/threading.h>
#include "gs/GSH_OpenGL/GSH_OpenGL.h"

class CGSH_OpenGLJs : public CGSH_OpenGL
{
public:
	CGSH_OpenGLJs(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE, bool gsThreaded = true);
	virtual ~CGSH_OpenGLJs() = default;

	static FactoryFunction GetFactoryFunction(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE, bool gsThreaded = true);

	void InitializeImpl() override;
	void ReleaseImpl() override;
	void PresentBackbuffer() override;

private:
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE m_context = 0;
};

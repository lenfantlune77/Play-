cmake_minimum_required(VERSION 3.18)

set(CMAKE_MODULE_PATH
	${CMAKE_CURRENT_SOURCE_DIR}/../../deps/Dependencies/cmake-modules
	${CMAKE_MODULE_PATH}
)
include(Header)

project(Play)

if(NOT TARGET PlayCore)
	add_subdirectory(
		${CMAKE_CURRENT_SOURCE_DIR}/../
		${CMAKE_CURRENT_BINARY_DIR}/Source
	)
endif()
list(APPEND UI_JS_PROJECT_LIBS PlayCore)

if(NOT TARGET ui_shared)
	add_subdirectory(
		${CMAKE_CURRENT_SOURCE_DIR}/../ui_shared
		${CMAKE_CURRENT_BINARY_DIR}/ui_shared
	)
endif()
list(APPEND UI_JS_PROJECT_LIBS ui_shared)

if(NOT TARGET gsh_opengl)
	add_subdirectory(
		${CMAKE_CURRENT_SOURCE_DIR}/../gs/GSH_OpenGL
		${CMAKE_CURRENT_BINARY_DIR}/gs/GSH_OpenGL
	)
endif()
list(APPEND UI_JS_PROJECT_LIBS gsh_opengl)

if(NOT TARGET sh_openal)
	add_subdirectory(
		${CMAKE_CURRENT_SOURCE_DIR}/../sound/SH_OpenAL
		${CMAKE_CURRENT_BINARY_DIR}/SH_OpenAL
	)
endif()
list(APPEND UI_JS_PROJECT_LIBS sh_openal)

add_executable(Play
	GSH_OpenGLJs.cpp
	GSH_OpenGLJs.h
	InputProviderEmscripten.cpp
	InputProviderEmscripten.h
	Main.cpp
	Ps2VmJs.cpp
	Ps2VmJs.h
)
target_link_libraries(Play PUBLIC ${UI_JS_PROJECT_LIBS})

target_link_options(Play PRIVATE "--bind")
target_link_options(Play PRIVATE "-sMODULARIZE=1")
target_link_options(Play PRIVATE "-sEXPORT_ES6=1")
target_link_options(Play PRIVATE "-sEXPORTED_FUNCTIONS=['_main', '_initVm', '_EmptyBlockHandler', '_MemoryUtils_GetByteProxy', '_MemoryUtils_GetHalfProxy', '_MemoryUtils_GetWordProxy', '_MemoryUtils_GetDoubleProxy', '_MemoryUtils_SetByteProxy', '_MemoryUtils_SetHalfProxy', '_MemoryUtils_SetWordProxy', '_MemoryUtils_SetDoubleProxy', '_MemoryUtils_LoadQuadProxy', '_MemoryUtils_StoreQuadProxy', '_Vu0Async_Pull', '_Vu0Async_Push', '_Vu0Async_Wait', '_Vu0Async_Signal', '_LWL_Proxy', '_LWR_Proxy', '_LDL_Proxy', '_LDR_Proxy', '_SWL_Proxy', '_SWR_Proxy', '_SDL_Proxy', '_SDR_Proxy', '_TrapHandler', '_HandleTLBException', '_HandleTLBRead_Proxy', '_HandleTLBWrite_Proxy', '_TranslateAddress_Proxy', '_TranslateAddressTLB_Proxy', '_TranslateAddress64_Proxy', '_CheckTLBExceptions_Proxy', '_FpAddTruncate_Proxy']")
target_link_options(Play PRIVATE "-sEXPORTED_RUNTIME_METHODS=['ccall', 'FS', 'HEAPU8', 'wasmMemory']")
target_link_options(Play PRIVATE "-sFORCE_FILESYSTEM")
target_link_options(Play PRIVATE "-sALLOW_TABLE_GROWTH")
target_link_options(Play PRIVATE "-sALLOW_MEMORY_GROWTH")
#Shared memory reserves its maximum up front. A phone refuses the 2GB default, a desktop
#welcomes it, so the build is made twice and the page picks the one that suits the device.
set(PLAY_MAXIMUM_MEMORY "512MB" CACHE STRING "Maximum heap of the browser build")
target_link_options(Play PRIVATE "-sMAXIMUM_MEMORY=${PLAY_MAXIMUM_MEMORY}")
#With this on, main() runs in a worker and the canvas is handed to it, so the thread that draws owns
#the graphics context instead of sending every command to the browser's main thread. It needs
#OffscreenCanvas, which older Safari lacks, hence a build option rather than a plain default.
option(PLAY_PROXY_TO_PTHREAD "Run the emulation in a worker that owns the canvas" OFF)
if(PLAY_PROXY_TO_PTHREAD)
	target_link_options(Play PRIVATE "-sPROXY_TO_PTHREAD=1")
	target_link_options(Play PRIVATE "-sOFFSCREENCANVAS_SUPPORT=1")
	target_link_options(Play PRIVATE "-sOFFSCREENCANVASES_TO_PTHREAD=#outputCanvas")
	target_compile_definitions(Play PRIVATE PLAY_GS_ON_CANVAS_THREAD)
endif()
#target_link_options(Play PRIVATE "-sASSERTIONS=2")
target_link_options(Play PRIVATE "-sEXPORT_NAME=Play")
target_link_options(Play PRIVATE "-sPTHREAD_POOL_SIZE=2")
target_link_options(Play PRIVATE "-sWASM_BIGINT")
target_link_options(Play PRIVATE "-sOFFSCREEN_FRAMEBUFFER=1")
target_link_options(Play PRIVATE "-sENVIRONMENT=web,worker")
target_link_options(Play PRIVATE "-sUSE_WEBGL2=1")

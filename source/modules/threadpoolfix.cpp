#include "module.h"
#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/InterfacePointers.hpp>
#include "detours.h"
#include "util.h"
#include "lua.h"
#define DEDICATED
#include "vstdlib/jobthread.h"

class CThreadPoolFixModule : public IModule
{
public:
	virtual void Init(CreateInterfaceFn* appfn, CreateInterfaceFn* gamefn) OVERRIDE;
	virtual void InitDetour(bool bPreServer) OVERRIDE;
	virtual const char* Name() { return "threadpoolfix"; };
	virtual int Compatibility() { return LINUX32 | LINUX64; }; 
	// NOTE: This is only an issue specific to Linux 32x DS so this doesn't need to support anything else.
	// NOTE2: 64x has different issues: https://github.com/Facepunch/garrysmod-issues/issues/5310 & https://github.com/Facepunch/garrysmod-issues/issues/6031
};

static CThreadPoolFixModule g_pThreadPoolFixModule;
IModule* pThreadPoolFixModule = &g_pThreadPoolFixModule;

#if ARCHITECTURE_IS_X86
static Detouring::Hook detour_CThreadPool_ExecuteToPriority;
static int hook_CThreadPool_ExecuteToPriority(IThreadPool* pool, void* idx, void* idx2)
{
	if (pool->GetJobCount() <= 0)
		return 0;

	return detour_CThreadPool_ExecuteToPriority.GetTrampoline<Symbols::CThreadPool_ExecuteToPriority>()(pool, idx, idx2);
}
#endif

#if ARCHITECTURE_IS_X86_64
static Detouring::Hook detour_CThreadPool_Start;
static bool hook_CThreadPool_Start(IThreadPool* pool, /*const*/ ThreadPoolStartParams_t& params, const char* pszName)
{
	if (V_stricmp(pszName, "FsAsyncIO") && !params.bEnableOnLinuxDedicatedServer)
	{
		params.bEnableOnLinuxDedicatedServer = true;
		DevMsg("[holylib - threadpoolfix] Manually fixed Filesystem threadpool! (%s)\n", pszName);
	}

	if (!params.bEnableOnLinuxDedicatedServer)
		DevMsg("[holylib - threadpoolfix] Threadpool will fail to start! (%s)\n", pszName != NULL ? pszName : "NULL"); // Just want to make sure

	return detour_CThreadPool_Start.GetTrampoline<Symbols::CThreadPool_Start>()(pool, params, pszName);
}
#endif

void CThreadPoolFixModule::Init(CreateInterfaceFn* appfn, CreateInterfaceFn* gamefn)
{
#if ARCHITECTURE_IS_X86_64
	if (g_pThreadPool->NumThreads() > 0)
	{
		DevMsg("holylib: Threadpool is already running? Skipping our fix.\n"); // Seems to currently work again in gmod but I'll just leave it in
		return;
	}

	ThreadPoolStartParams_t startParams;
	Util::StartThreadPool(g_pThreadPool, startParams);
#endif

	// NOTE: Check Filesystem threadpool of 64x, I have a feeling that it is broken. AsyncRead always seems to run the next tick / seems to never actually use any threads.
}

void CThreadPoolFixModule::InitDetour(bool bPreServer)
{
	if (!bPreServer)
		return;

	SourceSDK::ModuleLoader libvstdlib_loader("vstdlib");
#if ARCHITECTURE_IS_X86
	Detour::Create(
		&detour_CThreadPool_ExecuteToPriority, "CThreadPool::ExecuteToPriority",
		libvstdlib_loader.GetModule(), Symbols::CThreadPool_ExecuteToPrioritySym,
		(void*)hook_CThreadPool_ExecuteToPriority, m_pID
	);
#endif

#if ARCHITECTURE_IS_X86_64
	Detour::Create(
		&detour_CThreadPool_Start, "CThreadPool::Start",
		libvstdlib_loader.GetModule(), Symbols::CThreadPool_StartSym,
		(void*)hook_CThreadPool_Start, m_pID
	);
#endif
}
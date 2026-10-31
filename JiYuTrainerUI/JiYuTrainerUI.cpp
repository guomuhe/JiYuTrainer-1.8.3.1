// JiYuTrainerUI.cpp : 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "MainWindow.h"
#include "UpdaterWindow.h"
#include "ConfigWindow.h"
#include "BugReportWindow.h"
#include "../JiYuTrainer/AppPublic.h"
#include "../JiYuTrainer/JiYuTrainer.h"
#include "../JiYuTrainer/SysHlp.h"
#include "JiYuTrainerUI.h"

MainWindow *currentMainWindow = nullptr;
extern JTApp * currentApp;

static FILE* g_dbg = nullptr;
#define DBG(...) do { if (g_dbg) fprintf(g_dbg, __VA_ARGS__); fflush(g_dbg); } while(0)

static LONG WINAPI JTDebugVEH(PEXCEPTION_POINTERS p)
{
	DBG(">>> VEH EXCEPTION! code=0x%08X addr=%p\n",
		(unsigned)p->ExceptionRecord->ExceptionCode,
		(void*)p->ExceptionRecord->ExceptionAddress);

	uintptr_t fault = (uintptr_t)p->ExceptionRecord->ExceptionAddress;

	// Check known modules
	struct { const wchar_t* name; } checkMods[] = {
		{L"sciter.dll"},
		{L"JiYuTrainer.exe"},
		{L"JiYuTrainerHooks.dll"},
		{L"user32.dll"},
		{L"kernel32.dll"},
		{L"kernelbase.dll"},
		{L"gdi32.dll"},
		{L"msvcp_win.dll"},
		{L"ucrtbase.dll"},
	};
	DBG("  --- Module scan for fault addr %p ---\n", (void*)fault);
	for (auto& cm : checkMods) {
		HMODULE h = GetModuleHandleW(cm.name);
		if (h) {
			uintptr_t base = (uintptr_t)h;
			// Use VirtualQuery to get region size of PE header
			MEMORY_BASIC_INFORMATION mbi;
			VirtualQuery((void*)base, &mbi, sizeof(mbi));
			// SizeOfImage is in PE header, but VirtualQuery gives allocation granularity,
			// just check if fault falls in the allocation range
			wchar_t fname[MAX_PATH] = {0};
			GetModuleFileNameW(h, fname, MAX_PATH);
			char ansi[MAX_PATH];
			WideCharToMultiByte(CP_ACP, 0, fname, -1, ansi, MAX_PATH, NULL, NULL);
			if (fault >= base) {
				// Walk memory regions from base forward
				uintptr_t scan = base;
				bool found_in_module = false;
				for (int regions = 0; regions < 200; regions++) {
					MEMORY_BASIC_INFORMATION mq;
					if (!VirtualQuery((void*)scan, &mq, sizeof(mq))) break;
					uintptr_t region_end = scan + mq.RegionSize;
					if (fault >= scan && fault < region_end) {
						DBG("  >>> FAULT in %s: region [%p - %p) state=0x%lx protect=0x%lx\n",
							ansi, (void*)scan, (void*)region_end,
							(unsigned long)mq.State, (unsigned long)mq.Protect);
						found_in_module = true;
						break;
					}
					if (mq.State != MEM_COMMIT && mq.State != MEM_RESERVE) break;
					scan = region_end;
				}
				if (!found_in_module) {
					DBG("      %s at %p (fault not in committed region)\n", ansi, (void*)base);
				}
			} else {
				DBG("      %s at %p\n", ansi, (void*)base);
			}
		}
	}

	for (DWORD i = 0; i < p->ExceptionRecord->NumberParameters && i < 5; i++)
		DBG("  param[%lu] = 0x%llX\n", (unsigned long)i,
			(unsigned long long)p->ExceptionRecord->ExceptionInformation[i]);

	if (p->ExceptionRecord->ExceptionCode == 0xC0000005 &&
		p->ExceptionRecord->NumberParameters >= 2) {
		unsigned long long accessed = p->ExceptionRecord->ExceptionInformation[1];
		MEMORY_BASIC_INFORMATION mbi;
		if (VirtualQuery((void*)accessed, &mbi, sizeof(mbi))) {
			DBG("  Accessed addr 0x%llX: AllocationBase=%p State=0x%lx Protect=0x%lx\n",
			accessed, mbi.AllocationBase, (unsigned long)mbi.State, (unsigned long)mbi.Protect);
		} else {
			DBG("  VirtualQuery FAILED\n");
		}
	}

	if (p->ContextRecord) {
		CONTEXT* ctx = p->ContextRecord;
#ifdef _WIN64
		DBG("  RIP=%p RSP=%p\n", (void*)ctx->Rip, (void*)ctx->Rsp);
		DBG("  RAX=%p RBX=%p RCX=%p RDX=%p\n",
			(void*)ctx->Rax, (void*)ctx->Rbx, (void*)ctx->Rcx, (void*)ctx->Rdx);
		DBG("  RSI=%p RDI=%p R12=%p R13=%p R14=%p R15=%p\n",
			(void*)ctx->Rsi, (void*)ctx->Rdi,
			(void*)ctx->R12, (void*)ctx->R13, (void*)ctx->R14, (void*)ctx->R15);
#else
		DBG("  EIP=%p ESP=%p\n", (void*)ctx->Eip, (void*)ctx->Esp);
		DBG("  EAX=%p EBX=%p ECX=%p EDX=%p\n",
			(void*)ctx->Eax, (void*)ctx->Ebx, (void*)ctx->Ecx, (void*)ctx->Edx);
		DBG("  ESI=%p EDI=%p EBP=%p\n",
			(void*)ctx->Esi, (void*)ctx->Edi, (void*)ctx->Ebp);
#endif
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

// Test each component incrementally
struct T1 : public sciter::host<T1>, public sciter::event_handler, public TrainerWorkerCallback {
	HWND _h = nullptr;
	HINSTANCE get_resource_instance() { return (HINSTANCE)1; }
	HWND get_hwnd() { return _h; }
};

struct T2 : public T1 {
	NOTIFYICONDATA nid;
	std::wstring statusBuffer;
};

struct T3 : public T2 {
	HelpWindow* help = nullptr;
	AttackWindow* attack = nullptr;
	Logger* logger = nullptr;
	TrainerWorker* worker = nullptr;
};

struct T4 : public T3 {
	sciter::dom::element el1, el2, el3, el4, el5;
};

int JiYuTrainerUIRunMain()
{
	// 日志写到 %TEMP% 目录，保持 exe 目录干净
	wchar_t logpath[MAX_PATH];
	swprintf_s(logpath, L"%sjt_debug.log", SysHlp::GetTempPath());
	_wfopen_s(&g_dbg, logpath, L"w");
	AddVectoredExceptionHandler(1, JTDebugVEH);

	DBG("=== JiYuTrainerUIRunMain ===\n");
	DBG("currentApp = %p\n", (void*)currentApp);
	DBG("sizeof(MainWindow) = %zu\n", sizeof(MainWindow));

	// Step 3a: pre-init sciter globally
	SciterSetOption(NULL, SCITER_SET_SCRIPT_RUNTIME_FEATURES, ALLOW_FILE_IO | ALLOW_SOCKET_IO | ALLOW_EVAL | ALLOW_SYSINFO);
	DBG("OK: SciterSetOption(NULL, SCITER_SET_SCRIPT_RUNTIME_FEATURES)\n");

#if _DEBUG
	SciterSetOption(NULL, SCITER_SET_DEBUG_MODE, TRUE);
#endif
	SciterClassName();
	DBG("OK: SciterClassName\n");

	DBG("=== Creating MainWindow ===\n");
	currentMainWindow = new MainWindow();
	DBG("OK: MainWindow created at %p\n", (void*)currentMainWindow);

	DBG("=== RunLoop ===\n");
	int result = currentMainWindow->RunLoop();
	DBG("RunLoop returned %d\n", result);

	delete currentMainWindow;
	DBG("=== DONE ===\n");
	fclose(g_dbg);
	return result;
}
int JiYuTrainerUIRunUpdate()
{
	UpdaterWindow updaterWindow(NULL);
	return updaterWindow.RunLoop();
}
int JiYuTrainerUIRunConfig()
{
	ShowMoreSettings(GetDesktopWindow());
	return 0;
}
int JiYuTrainerUIRunBugReport()
{
	ShowBugReportWindow();
	return 0;
}

UIEXPORT_CFUNC(int) JiYuTrainerUICommonEntry(int i)
{
	SetUnhandledExceptionFilter(AppUnhandledExceptionFilter);
	switch (i)
	{
	case 0: return JiYuTrainerUIRunMain();
	case 1: return JiYuTrainerUIRunUpdate();
	case 2: return JiYuTrainerUIRunConfig();
	case 3: return JiYuTrainerUIRunBugReport();
	case 4: currentApp->RunOperation(AppOperation3);  return 0;
	default: return 0;
	}
}

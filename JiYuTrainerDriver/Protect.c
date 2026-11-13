#include "protect.h"

PPROTECT_PROC_STORAGE protectProcsStart = NULL;
PPROTECT_PROC_STORAGE protectProcsEnd = NULL;
PVOID obHandle = NULL;
BOOLEAN protectInited = FALSE;
BOOLEAN protectXPHooked = FALSE;
BOOLEAN shadowSsdtHooked = FALSE;

PKSERVICE_TABLE_DESCRIPTOR KeServiceDescriptorTableShadow = NULL;

extern ObRegisterCallbacks_ _ObRegisterCallbacks;
extern ObUnRegisterCallbacks_ _ObUnRegisterCallbacks;
extern ObGetFilterVersion_ _ObGetFilterVersion;
extern memset_ _memset;
fnZwOpenProcess _ZwOpenProcess;
fnZwTerminateProcess _ZwTerminateProcess;
fnZwOpenProcess OldZwOpenProcess;
fnZwTerminateProcess OldZwTerminateProcess;

extern ULONG systemVersion;

//存放保护进程的一个链表
VOID KxInitProtectProcessList()
{
	protectProcsStart = ExAllocatePool(NonPagedPool, sizeof(PROTECT_PROC_STORAGE));
	protectProcsStart->Next = NULL;
	protectProcsStart->ProcessId = 0;
	protectProcsEnd = protectProcsStart;
}
VOID KxDestroyProtectProcessList()
{
	PPROTECT_PROC_STORAGE ptr = protectProcsStart;
	if (ptr->Next != NULL) {
		do {
			PPROTECT_PROC_STORAGE ptr_next = ptr->Next;
			ExFreePool(ptr);
			ptr = ptr_next;
		} while (ptr != NULL);
	}
	else ExFreePool(ptr);
}
BOOLEAN KxIsProcessProtect(HANDLE pid)
{
	//检测pid是否被保护
	if (pid == 0)return FALSE;
	PPROTECT_PROC_STORAGE ptr = protectProcsStart;
	if (ptr->Next != NULL) {
		do {
			if (ptr->ProcessId == pid)
				return TRUE;
			ptr = ptr->Next;
		} while (ptr != NULL);
	}
	return FALSE;
}
VOID KxProtectProcessWithPid(HANDLE pid)
{
	if (pid == 0) return;
	if (!KxIsProcessProtect(pid))
	{
		if (protectProcsEnd)
		{
			//添加到链表末尾
			protectProcsEnd->Next = ExAllocatePool(NonPagedPool, sizeof(PROTECT_PROC_STORAGE));
			_memset(protectProcsEnd->Next, 0, sizeof(PROTECT_PROC_STORAGE));
			((PPROTECT_PROC_STORAGE)protectProcsEnd->Next)->ProcessId = pid;
			protectProcsEnd = protectProcsEnd->Next;

			KdPrint(("Protect Process : %d", pid));
		}
	}
	else KdPrint(("Protect Process : %d alreday protected", pid));
}
VOID KxUnProtectProcessWithPid(HANDLE pid)
{
	if (pid == 0)return;
	PPROTECT_PROC_STORAGE ptr = protectProcsStart;
	PPROTECT_PROC_STORAGE ptr_for = protectProcsStart;
	if (ptr->Next != NULL) {
		do {
			//链表删除项目
			if (ptr->ProcessId == pid) {
				if (ptr_for) {
					if (ptr->Next == NULL) {
						ptr_for->Next = NULL;
						protectProcsEnd = ptr_for;
					}
					else ptr_for->Next = ptr->Next;
				}
				KdPrint(("UnProtect Process : %d", pid));
				ExFreePool(ptr);
				return;
			}
			else {
				ptr_for = ptr;
				ptr = ptr->Next;
			}
		} while (ptr != NULL);
	}
}

//初始化和退出
NTSTATUS KxInitProtectProcess()
{
	if (!protectInited) {

		protectInited = TRUE;

		KxInitProtectProcessList();

		if (!_ObRegisterCallbacks || !_ObGetFilterVersion)
			return STATUS_NOT_SUPPORTED;

		OB_CALLBACK_REGISTRATION obReg;
		OB_OPERATION_REGISTRATION opReg;

		memset(&obReg, 0, sizeof(obReg));
		obReg.Version = _ObGetFilterVersion();
		obReg.OperationRegistrationCount = 1;
		obReg.RegistrationContext = NULL;
		RtlInitUnicodeString(&obReg.Altitude, L"321000");

		memset(&opReg, 0, sizeof(opReg)); //初始化				  
		opReg.ObjectType = PsProcessType;//进程
		opReg.Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
		opReg.PreOperation = (POB_PRE_OPERATION_CALLBACK)&KxObPreCall; //在这里注册一个回调函数指针
		obReg.OperationRegistration = &opReg; //注意这一条语句
		return _ObRegisterCallbacks(&obReg, &obHandle); //在这里注册回调函数
	}
	return TRUE;
}
VOID KxUnInitProtectProcess()
{
	if(obHandle) _ObUnRegisterCallbacks(obHandle); //取消注册回调
	if (protectInited) {
		KxDestroyProtectProcessList();
		protectInited = FALSE;
	}
	//【仅限授权测试环境】XP 时代的 SSDT 还原随 JT_ENABLE_LEGACY_SSDT 一起编译，默认不存在
#if JT_ENABLE_LEGACY_SSDT
	if (protectXPHooked) {
		KxUnHookInXP();
	}
#endif
}

//回调函数
OB_PREOP_CALLBACK_STATUS KxObPreCall(PVOID RegistrationContext, POB_PRE_OPERATION_INFORMATION pOperationInformation)
{
	if (pOperationInformation->KernelHandle || pOperationInformation->ObjectType != *PsProcessType)
		return OB_PREOP_SUCCESS;
	HANDLE pid = PsGetProcessId((PEPROCESS)pOperationInformation->Object);
	UNREFERENCED_PARAMETER(RegistrationContext);
	if (KxIsProcessProtect(pid))
	{
		if (pOperationInformation->Operation == OB_OPERATION_HANDLE_CREATE || pOperationInformation->Operation == OB_OPERATION_HANDLE_DUPLICATE)
		{
			if ((pOperationInformation->Parameters->CreateHandleInformation.OriginalDesiredAccess & PROCESS_TERMINATE) == PROCESS_TERMINATE)
				pOperationInformation->Parameters->CreateHandleInformation.DesiredAccess &= ~PROCESS_TERMINATE;
			if ((pOperationInformation->Parameters->CreateHandleInformation.OriginalDesiredAccess & PROCESS_VM_OPERATION) == PROCESS_VM_OPERATION)
				pOperationInformation->Parameters->CreateHandleInformation.DesiredAccess &= ~PROCESS_VM_OPERATION;
			if ((pOperationInformation->Parameters->CreateHandleInformation.OriginalDesiredAccess & PROCESS_VM_READ) == PROCESS_VM_READ)
				pOperationInformation->Parameters->CreateHandleInformation.DesiredAccess &= ~PROCESS_VM_READ;
			if ((pOperationInformation->Parameters->CreateHandleInformation.OriginalDesiredAccess & PROCESS_VM_WRITE) == PROCESS_VM_WRITE)
				pOperationInformation->Parameters->CreateHandleInformation.DesiredAccess &= ~PROCESS_VM_WRITE;
		}
	}
	return OB_PREOP_SUCCESS;
}

//Hook functions

NTSTATUS NTAPI Hook_ZwOpenProcess(_Out_ PHANDLE ProcessHandle, _In_ ACCESS_MASK DesiredAccess, _In_ POBJECT_ATTRIBUTES ObjectAttributes, _In_opt_ PCLIENT_ID ClientId) {
	if (KxIsProcessProtect(ClientId->UniqueProcess)) 
		return STATUS_ACCESS_DENIED;
	return OldZwOpenProcess(ProcessHandle, DesiredAccess, ObjectAttributes, ClientId);
}
NTSTATUS NTAPI Hook_ZwTerminateProcess(__in_opt HANDLE ProcessHandle, __in NTSTATUS ExitStatus)
{
	HANDLE uPID = 0;
	NTSTATUS ntStatus = 0;

	PROCESS_BASIC_INFORMATION pbi;
	ntStatus = ZwQueryInformationProcess(ProcessHandle, ProcessBasicInformation, (PVOID)&pbi, sizeof(pbi), NULL);
	if (!NT_SUCCESS(ntStatus))
		return ntStatus;

	uPID = (HANDLE)pbi.UniqueProcessId;
	if (KxIsProcessProtect(uPID))//不是当前进程PID，为了客户端自己关自己
	{
		if (uPID != PsGetProcessId(PsGetCurrentProcess()))
			return STATUS_ACCESS_DENIED;
	}

	ntStatus = OldZwTerminateProcess(ProcessHandle, ExitStatus);
	return ntStatus;
}

//SSDT HOOK For process in xp
//=============================================================================
//【仅限授权测试环境】以下 SSDT / Shadow SSDT 改写仅在 JT_ENABLE_LEGACY_SSDT=1
//时才会编译。该机制只适用于 XP 时代的系统，在 Win10+ 上会触发 PatchGuard
//（BSOD），在启用 HVCI/VBS 的系统上会被直接拒绝。默认构建不包含这些代码。
//=============================================================================
#if JT_ENABLE_LEGACY_SSDT

VOID KxHookInXP() 
{
	if (!protectXPHooked && systemVersion == SYS_BULID_VERSION_XP) {

		DbgPrint("KxHookInXP\n");

		OldZwTerminateProcess = (fnZwTerminateProcess)KeServiceDescriptorTable[0].Base[SYSCALL_INDEX(ZwTerminateProcess)];
		OldZwOpenProcess = (fnZwOpenProcess)KeServiceDescriptorTable[0].Base[SYSCALL_INDEX(ZwOpenProcess)];

		DbgPrint("SSTD ZwTerminateProcess old:  0x%08X\nSSTD ZwOpenProcess old:  0x%08X\n", 
			OldZwTerminateProcess, OldZwOpenProcess);

		ULONG uOldAttr = 0;
		EnableWriteProtect(&uOldAttr);
		KeServiceDescriptorTable[0].Base[SYSCALL_INDEX(ZwTerminateProcess)] = (ULONG)Hook_ZwTerminateProcess;
		DisableWriteProtect(uOldAttr);
		EnableWriteProtect(&uOldAttr);
		KeServiceDescriptorTable[0].Base[SYSCALL_INDEX(ZwOpenProcess)] = (ULONG)Hook_ZwOpenProcess;
		DisableWriteProtect(uOldAttr);

		protectXPHooked = TRUE;
		protectInited = TRUE;
	}
}
VOID KxUnHookInXP()
{
	if (protectXPHooked) {

		ULONG uOldAttr = 0;
		EnableWriteProtect(&uOldAttr);
		KeServiceDescriptorTable[0].Base[SYSCALL_INDEX(ZwTerminateProcess)] = (ULONG)OldZwTerminateProcess;
		DisableWriteProtect(uOldAttr);
		EnableWriteProtect(&uOldAttr);
		KeServiceDescriptorTable[0].Base[SYSCALL_INDEX(ZwOpenProcess)] = (ULONG)OldZwOpenProcess;
		DisableWriteProtect(uOldAttr);

		DbgPrint("KxUnHookInXP\n");

		protectXPHooked = FALSE;
	}
}

//Shadow SSDT HOOK For window protect 

VOID KxShadowSSDTHook() 
{
	if (!shadowSsdtHooked) {

		DbgPrint("KxShadowSSDTHook\n");

		if(systemVersion == SYS_BULID_VERSION_XP)
			KeServiceDescriptorTableShadow = (ULONG_PTR)(KeServiceDescriptorTable - 0x40);
		else if (systemVersion == SYS_BULID_VERSION_XP)
			KeServiceDescriptorTableShadow = (ULONG_PTR)(KeServiceDescriptorTable - 0x40);

		DbgPrint("KeServiceDescriptorTableShadow : 0x%08X\n", KeServiceDescriptorTableShadow);

		if (KeServiceDescriptorTableShadow) {



			shadowSsdtHooked = TRUE;
		}
	}
}
VOID KxShadowSSDTUnHook()
{
	if (shadowSsdtHooked) {


		shadowSsdtHooked = FALSE;
	}
}

//
//HOOKS
//

VOID DisableWriteProtect(ULONG oldAttr)
{
	_asm
	{
		mov eax, oldAttr
		mov cr0, eax
		sti;
	}
}

#endif //JT_ENABLE_LEGACY_SSDT

VOID EnableWriteProtect(PULONG pOldAttr)
{
	ULONG uAttr;
	_asm
	{
		cli;
		mov  eax, cr0;
		mov  uAttr, eax;
		and  eax, 0FFFEFFFFh; // CR0 16 BIT = 0 
		mov  cr0, eax;
	};
	//保存原有的 CRO 属性 
	*pOldAttr = uAttr;
}
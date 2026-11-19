#include "Memory.h"

BOOLEAN WriteOnReadOnlyMemory(PVOID src, PVOID dst, size_t size) {

	PMDL mdl = NULL;
	PVOID mapped = NULL;

	if (size == 0) return TRUE;

	if (src == NULL || dst == NULL) {
		return FALSE;
	}

	// KDU-mapped drivers have no registered exception handlers (no SEH support),
	// so validate the destination pages up front instead of relying on __try/__except.
	for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
		if (!MmIsAddressValid((PUCHAR)dst + offset)) {
			return FALSE;
		}
	}

	if (!MmIsAddressValid((PUCHAR)dst + size - 1)) {
		return FALSE;
	}

	mdl = IoAllocateMdl(dst, (ULONG)size, FALSE, FALSE, NULL);

	if (mdl == NULL) {
		return FALSE;
	}

	MmProbeAndLockPages(mdl, KernelMode, IoReadAccess);

	mapped = MmMapLockedPagesSpecifyCache(mdl, KernelMode, MmNonCached, NULL, FALSE, NormalPagePriority);
	if (mapped == NULL) {
		MmUnlockPages(mdl);
		IoFreeMdl(mdl);
		return FALSE;
	}

	MmProtectMdlSystemAddress(mdl, PAGE_READWRITE);
	RtlCopyMemory(mapped, src, size);

	MmUnmapLockedPages(mapped, mdl);
	MmUnlockPages(mdl);
	IoFreeMdl(mdl);

	return TRUE;
}



BOOLEAN HookTrampoline(PVOID origFunction, PVOID hookFunction, PVOID gateway, size_t len) {

	UCHAR detourTemplate[] = {
		0xFF, 0x25, 0, 0, 0, 0
	};

	if (len < sizeof(detourTemplate) + 8) {
		return FALSE;
	}

	if (!WriteOnReadOnlyMemory(origFunction, gateway, len)) {
		return FALSE;
	}

	if (!WriteOnReadOnlyMemory(detourTemplate, (PVOID)((uintptr_t)gateway + len), sizeof(detourTemplate))) {
		return FALSE;
	}

	uintptr_t returnAddress = (uintptr_t)origFunction + len;
	if (!WriteOnReadOnlyMemory(&returnAddress, (PVOID)((uintptr_t)gateway + len + sizeof(detourTemplate)), 8)) {
		return FALSE;
	}

	if (!WriteOnReadOnlyMemory(detourTemplate, origFunction, sizeof(detourTemplate))) {
		return FALSE;
	}

	uintptr_t detourAddress = (uintptr_t)hookFunction;
	if (!WriteOnReadOnlyMemory(&detourAddress, (PVOID)((uintptr_t)origFunction + sizeof(detourTemplate)), 8)) {
		return FALSE;
	}

	// So, the gateway is like this:
	// .. (original code) .. | jmp [rip+0x00] | Orig.

	// And the original function is like this:
	// jmp [rip+0x00] | Hook

	return TRUE;
}
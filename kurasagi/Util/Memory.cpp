/*
 * @file Memory.cpp
 * @brief Implementation for Memory.hpp
 */

#include "Memory.hpp"
#include "../Log.hpp"
#include "../Global.hpp"

BOOLEAN WriteOnReadOnlyMemory(PVOID src, PVOID dst, size_t size) {

	PMDL mdl = NULL;
	PVOID mapped = NULL;

	if (size == 0) return TRUE;

	if (src == NULL || dst == NULL) {
		LogError("WriteOnReadOnlyMemory: Invalid parameter");
		return FALSE;
	}

	// KDU-mapped drivers have no registered exception handlers (no SEH support),
	// so validate the destination pages up front instead of relying on __try/__except.
	for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
		if (!MmIsAddressValid((PUCHAR)dst + offset)) {
			LogError("WriteOnReadOnlyMemory: Destination page %p is not valid", (PUCHAR)dst + offset);
			return FALSE;
		}
	}

	if (!MmIsAddressValid((PUCHAR)dst + size - 1)) {
		LogError("WriteOnReadOnlyMemory: Destination end %p is not valid", (PUCHAR)dst + size - 1);
		return FALSE;
	}

	mdl = IoAllocateMdl(dst, (ULONG)size, FALSE, FALSE, NULL);
	if (mdl == NULL) {
		LogError("WriteOnReadOnlyMemory: Mdl allocation failed");
		return FALSE;
	}

	MmProbeAndLockPages(mdl, KernelMode, IoReadAccess);

	mapped = MmMapLockedPagesSpecifyCache(mdl, KernelMode, MmNonCached, NULL, FALSE, NormalPagePriority);
	if (mapped == NULL) {
		LogError("WriteOnReadOnlyMemory: MmMapLockedPagesSpecifyCache failed");
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


BOOLEAN IsCanonicalAddress(PVOID address) {
	UINT64 bit47 = ((UINT64)address >> 47) & 1;
	if (bit47 * 0xFFFF != ((UINT64)address >> 48)) {
		LogVerbose("IsCanonicalAddress: address %p is not canonical address!", address);
		return FALSE;
	}
	return TRUE;
}

UCHAR GetPml4eVaType(size_t index) {
	if (!gl::RtVar::MiVisibleStatePtr) {
		LogError("GetPml4eVaType: MiVisibleStatePtr not initialized");
		return 0xFF;
	}

	if (index < 256 || index >= 512) {
		LogError("GetPml4eVaType: Invalid index parameter");
		return 0xFF;
	}

	return ((PMI_VISIBLE_STATE_STUB)gl::RtVar::MiVisibleStatePtr)->SystemVaType[index - 256];
}

UINT64* GetPageTableEntryPointer(PVOID v, size_t level) {
	if (level == 0 || level > 4) {
		LogError("GetPageTableEntryPointer: Level is invalid!");
		return NULL;
	}

	UINT64* ptePointer = NULL;

	if (level == 1) { // Pt
		ptePointer = (UINT64*)(gl::RtVar::Pte::MmPteBase + (((ULONG64)v >> 9) & 0x7F'FFFF'FFF8));
	}
	else if (level == 2) { // Pd
		ptePointer = (UINT64*)(gl::RtVar::Pte::MmPdeBase + (((ULONG64)v >> 18) & 0x3FFF'FFF8));
	}
	else if (level == 3) { // Pdpt
		ptePointer = (UINT64*)(gl::RtVar::Pte::MmPdpteBase + (((ULONG64)v >> 27) & 0x1F'FFF8));
	}
	else { // level == 4, Pml4
		ptePointer = (UINT64*)gl::RtVar::Pte::MmPml4eBase + (((ULONG64)v >> 39) & 0x1FF);
	}
	
	return ptePointer;
}

UINT64* GetLastPageTableEntryPointer(PVOID v) {

	UINT64* pte = NULL;
	for (size_t level = 4; level > 0; level--) {
		pte = GetPageTableEntryPointer(v, level);
		if (*pte & 1)
			break;
		else
			pte = NULL;
	}
	return pte;
}

PVOID MakeCanonicalAddress(PVOID address) {

	uintptr_t trimmedAddress = ((uintptr_t)address << 16) >> 16;
	
	if ((trimmedAddress >> 47) & 0x1) {
		trimmedAddress |= 0xFFFF'0000'0000'0000;
	}

	return (PVOID)trimmedAddress;
}
 
size_t GetPml4Index(PVOID address) {
	return ((uintptr_t)address >> 39) & 0x1ff;
}

BOOLEAN IsValidAddress(PVOID address) {
	if (!IsCanonicalAddress(address)) return FALSE;
	if (GetLastPageTableEntryPointer(address)) return TRUE;
	else return FALSE;
}

BOOLEAN Hook::HookTrampoline(PVOID origFunction, PVOID hookFunction, PVOID gateway, size_t len) {

	UCHAR detourTemplate[] = {
		0xFF, 0x25, 0, 0, 0, 0
	};

	if (len < sizeof(detourTemplate) + 8) {
		LogError("HookTrampoline: length is invalid, should be greater than %llu", sizeof(detourTemplate) + 8);
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

NTSTATUS GetModuleInformation(const char* szModuleName, PSYSTEM_MODULE_ENTRY outTargetModule) {
	if (!gl::RtVar::ZwQuerySystemInformationPtr) {
		LogError("GetModuleInformation: ZwQuerySystemInformationPtr not initialized");
		return STATUS_UNSUCCESSFUL;
	}

	ULONG infoLen = 0;
	auto status = gl::RtVar::ZwQuerySystemInformationPtr(SystemModuleInformation, &infoLen, 0, &infoLen);
	// It is okay to do this way
	// It should return 0xC0000004, but it is fine - it fills infoLen

	PSYSTEM_MODULE_INFORMATION pMod = (PSYSTEM_MODULE_INFORMATION)ExAllocatePool2(POOL_FLAG_NON_PAGED, infoLen, KURASAGI_POOL_TAG);
	PSYSTEM_MODULE_ENTRY pModEntry = nullptr;

	if (!pMod) {
		LogError("GetModuleInformation: Allocation Failed");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = gl::RtVar::ZwQuerySystemInformationPtr(SystemModuleInformation, pMod, infoLen, &infoLen);
	if (!NT_SUCCESS(status)) {
		LogError("GetModuleInformation: Second ZwQuery Failed: %x", status);
		goto GMI_FREE_AND_RETURN;
	}

	pModEntry = pMod->Module;
	for (ULONG i = 0; i < pMod->Count; i++) {
		if (!_stricmp((const char*)pModEntry[i].FullPathName, szModuleName))
		{
			*outTargetModule = pModEntry[i];
			status = STATUS_SUCCESS;
			goto GMI_FREE_AND_RETURN;
		}
	}

	LogError("GetModuleInformation: Not Found");
	status = STATUS_NOT_FOUND;

GMI_FREE_AND_RETURN:
	ExFreePoolWithTag(pMod, KURASAGI_POOL_TAG);
	return status;
}

BOOLEAN GetKernelBaseNSize(uintptr_t* outBase, size_t* outSize) {
	SYSTEM_MODULE_ENTRY entry = { 0 };
	auto status = GetModuleInformation("\\SystemRoot\\system32\\ntoskrnl.exe", &entry);

	if (!NT_SUCCESS(status)) {
		LogError("GetKernelBaseNSize: Not Found");
		return FALSE;
	}

	*outBase = (uintptr_t)entry.ImageBase;
	*outSize = entry.ImageSize;

	return TRUE;
}

BOOLEAN FindPeSectionByName(size_t base, const char sectionName[8], uintptr_t* outSectionBase, size_t* outSectionSize) {

	if (!base || !sectionName || !outSectionBase || !outSectionSize) {
		LogError("FindPeSectionByName: Invalid Parameter");
		return FALSE;
	}

	IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;

	if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
INVALID_IMAGE_BASE:
		LogError("FindPeSectionByName: Invalid Image Base");
		return FALSE;
	}

	IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) {
		goto INVALID_IMAGE_BASE;
	}

	IMAGE_FILE_HEADER* file = &nt->FileHeader;
	IMAGE_SECTION_HEADER* sec = (IMAGE_SECTION_HEADER*)((uintptr_t)&nt->OptionalHeader + file->SizeOfOptionalHeader);

	USHORT nsec = file->NumberOfSections;
	for (USHORT i = 0; i < nsec; i++) {
		if (RtlEqualMemory(sec[i].Name, sectionName, 8)) {
			size_t vSize = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
			*outSectionBase = base + sec[i].VirtualAddress;
			*outSectionSize = vSize;
			LogVerbose("Section Base: %llx, Size: %llx", base + sec[i].VirtualAddress, vSize);
			return TRUE;
		}
	}

	LogError("FindPeSectionByName: Not found section");
	return FALSE;
}

BOOLEAN PatternSearchRange(unsigned char* start, unsigned char* end, const UCHAR* pattern, const char* mask, uintptr_t* result) {

	if (!start || !end || !pattern || !mask || !result) {
		LogError("PatternSearchRange: Invalid Parameter");
		return FALSE;
	}

	size_t m = strlen(mask);

	for (auto ptr = start; ptr + m <= end; ptr++) {
		size_t j = 0;
		for (; j < m; j++) {
			if (mask[j] == 'x' && ptr[j] != pattern[j]) break;
		}
		if (j == m) {
			*result = (uintptr_t)ptr;
			return TRUE;
		}
	}

	LogError("PatternSearchRange: Not Found");
	return FALSE;
}

BOOLEAN PatternSearchNtKernelSection(const char sectionName[8], const UCHAR* pattern, const char* mask, uintptr_t* result) {
	if (!gl::RtVar::KernelBase) {
		LogError("PatternSearchNtKernelSection: Kernel Base not initialized");
		return FALSE;
	}

	uintptr_t sectionBase = 0;
	size_t sectionSize = 0;
	if (!FindPeSectionByName(gl::RtVar::KernelBase, sectionName, &sectionBase, &sectionSize)) {
		return FALSE;
	}

	unsigned char* start = (unsigned char*)sectionBase;
	unsigned char* end = (unsigned char*)(sectionBase + sectionSize);

	if (!PatternSearchRange(start, end, pattern, mask, result)) {
		return FALSE;
	}

	return TRUE;
}

uintptr_t GetAbsAddrFromRel4B(unsigned int* Rel4BAddr) {

	// There's no x86 instruction that executes like this: 48 8B 05 ?? ?? ?? ?? (Const)...
	// So we can safely add 1 (4 bytes) to the address (to calculate next RIP)
	auto ripNext = (uintptr_t)(Rel4BAddr + 1);
	auto offset = *Rel4BAddr;

	return ripNext + offset;
}
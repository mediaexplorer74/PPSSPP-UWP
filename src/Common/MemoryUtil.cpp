// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "ppsspp_config.h"

#include <cstring>
#include <cstdlib>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/MemoryUtil.h"
#include "Common/StringUtils.h"
#include "Common/SysError.h"
#include "Core/Config.h"
//#include "UWP/UWPHelpers/StorageExtensions.h"

#ifdef _WIN32
#include "Common/CommonWindows.h"
#else
#include <errno.h>
#include <stdio.h>
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/mman.h>
#include <mach/vm_param.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#endif
static int hint_location;
#ifdef __APPLE__
#define MEM_PAGE_SIZE (PAGE_SIZE)
#elif defined(_WIN32)
static SYSTEM_INFO sys_info;
#define MEM_PAGE_SIZE (uintptr_t)(sys_info.dwPageSize)
#else
#define MEM_PAGE_SIZE (getpagesize())
#endif

#define MEM_PAGE_MASK ((MEM_PAGE_SIZE)-1)
#define ppsspp_round_page(x) ((((uintptr_t)(x)) + MEM_PAGE_MASK) & ~(MEM_PAGE_MASK))

#ifdef _WIN32
// Win32 memory protection flags are odd...
static uint32_t ConvertProtFlagsWin32(uint32_t flags) {
	uint32_t protect = 0;
	switch (flags) {
	case 0: protect = PAGE_NOACCESS; break;
	case MEM_PROT_READ: protect = PAGE_READONLY; break;
	case MEM_PROT_WRITE: protect = PAGE_READWRITE; break;   // Can't set write-only
	case MEM_PROT_EXEC: protect = PAGE_EXECUTE; break;
	case MEM_PROT_READ | MEM_PROT_EXEC: protect = PAGE_EXECUTE_READ; break;
	case MEM_PROT_WRITE | MEM_PROT_EXEC: protect = PAGE_EXECUTE_READWRITE; break;  // Can't set write-only
	case MEM_PROT_READ | MEM_PROT_WRITE: protect = PAGE_READWRITE; break;
	case MEM_PROT_READ | MEM_PROT_WRITE | MEM_PROT_EXEC: protect = PAGE_EXECUTE_READWRITE; break;
	}
	return protect;
}

#else

static uint32_t ConvertProtFlagsUnix(uint32_t flags) {
	uint32_t protect = 0;
	if (flags & MEM_PROT_READ)
		protect |= PROT_READ;
	if (flags & MEM_PROT_WRITE)
		protect |= PROT_WRITE;
	if (flags & MEM_PROT_EXEC)
		protect |= PROT_EXEC;
	return protect;
}

#endif

static uintptr_t last_executable_addr;
static void *SearchForFreeMem(size_t size) {
	if (!last_executable_addr)
		last_executable_addr = (uintptr_t) &hint_location - sys_info.dwPageSize;
	last_executable_addr -= size;

	MEMORY_BASIC_INFORMATION info;
	while (VirtualQuery((void *)last_executable_addr, &info, sizeof(info)) == sizeof(info)) {
		// went too far, unusable for executable memory
		if (last_executable_addr + 0x80000000 < (uintptr_t) &hint_location)
			return NULL;

		uintptr_t end = last_executable_addr + size;
		if (info.State != MEM_FREE)
		{
			last_executable_addr = (uintptr_t) info.AllocationBase - size;
			continue;
		}

		if ((uintptr_t)info.BaseAddress + (uintptr_t)info.RegionSize >= end &&
			(uintptr_t)info.BaseAddress <= last_executable_addr)
			return (void *)last_executable_addr;

		last_executable_addr -= size;
	}

	return NULL;
}

LPVOID(WINAPI* VirtualAllocPtr)(LPVOID, SIZE_T, DWORD, DWORD) = NULL;
BOOL(WINAPI* VirtualProtectPtr)(LPVOID, SIZE_T, ULONG, PULONG) = NULL;

// This is purposely not a full wrapper for virtualalloc/mmap, but it
// provides exactly the primitive operations that PPSSPP needs.
void *AllocateExecutableMemory(size_t size) {
	void *ptr = nullptr;
	DWORD prot = PAGE_EXECUTE_READWRITE;
	
	if (sys_info.dwPageSize == 0)
		GetSystemInfo(&sys_info);
	if ((uintptr_t)&hint_location > 0xFFFFFFFFULL) {
		size_t aligned_size = ppsspp_round_page(size);
#if 1   // Turn off to hunt for RIP bugs on x86-64.
		ptr = SearchForFreeMem(aligned_size);
		if (!ptr) {
			// Let's try again, from the top.
			// When we deallocate, this doesn't change, so we eventually run out of space.
			last_executable_addr = 0;
			ptr = SearchForFreeMem(aligned_size);
		}
#endif
		if (ptr) {
			ptr = VirtualAllocPtr(ptr, aligned_size, MEM_RESERVE | MEM_COMMIT, prot);
		}
		else {
			WARN_LOG(COMMON, "Unable to find nearby executable memory for jit. Proceeding with far memory.");
			// Can still run, thanks to "RipAccessible".
			ptr = VirtualAllocPtr(nullptr, aligned_size, MEM_RESERVE | MEM_COMMIT, prot);
		}
	}
	else
	{
		ptr = VirtualAllocPtr(0, size, MEM_RESERVE | MEM_COMMIT, prot);
	}

	static const void* failed_result = nullptr;

	if (ptr == failed_result) {
		ptr = nullptr;
		ERROR_LOG(MEMMAP, "Failed to allocate executable memory (%d) errno=%d", (int)size, errno);
	}

	return ptr;
}

void *AllocateMemoryPages(size_t size, uint32_t memProtFlags) {
	size = ppsspp_round_page(size);
	if (sys_info.dwPageSize == 0)
		GetSystemInfo(&sys_info);
	uint32_t protect = ConvertProtFlagsWin32(memProtFlags);

	void* ptr = VirtualAllocPtr(0, size, MEM_COMMIT, protect);
	if (!ptr) {
		ERROR_LOG(MEMMAP, "Failed to allocate raw memory pages");
		return nullptr;
	}

	return ptr;
}

void *AllocateAlignedMemory(size_t size, size_t alignment) {
	void* ptr = _aligned_malloc(size, alignment);

	_assert_msg_(ptr != nullptr, "Failed to allocate aligned memory");
	return ptr;
}

void FreeMemoryPages(void *ptr, size_t size) {
	if (!ptr)
		return;

	uintptr_t page_size = GetMemoryProtectPageSize();
	size = (size + page_size - 1) & (~(page_size - 1));
	if (!VirtualFree(ptr, 0, MEM_RELEASE)) {
		ERROR_LOG(MEMMAP, "FreeMemoryPages failed!\n%s", GetLastErrorMsg().c_str());
	}
}

void FreeAlignedMemory(void* ptr) {
	if (!ptr)
		return;
	_aligned_free(ptr);
}

bool PlatformIsWXExclusive() {
	return false;
}

bool ProtectMemoryPages(const void* ptr, size_t size, uint32_t memProtFlags) {
	VERBOSE_LOG(JIT, "ProtectMemoryPages: %p (%d) : r%d w%d x%d", ptr, (int)size,
			(memProtFlags & MEM_PROT_READ) != 0, (memProtFlags & MEM_PROT_WRITE) != 0, (memProtFlags & MEM_PROT_EXEC) != 0);

	// Note - VirtualProtect will affect the full pages containing the requested range.
	// mprotect does not seem to, at least not on Android unless I made a mistake somewhere, so we manually round.
	uint32_t protect = ConvertProtFlagsWin32(memProtFlags);

	DWORD oldValue;
	if (!VirtualProtectPtr((void*)ptr, size, protect, &oldValue)) {
		ERROR_LOG(MEMMAP, "WriteProtectMemory failed!\n%s", GetLastErrorMsg().c_str());
		return false;
	}
	return true;
}

int GetMemoryProtectPageSize() {
	if (sys_info.dwPageSize == 0)
		GetSystemInfo(&sys_info);
	return sys_info.dwPageSize;
}

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

#ifdef _WIN32

#include "MemArena.h"
#include "CommonWindows.h"

HANDLE(WINAPI* CreateFileMappingWPtr)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCWSTR);
LPVOID(WINAPI* MapViewOfFileExPtr)(HANDLE, DWORD, DWORD, DWORD, SIZE_T, LPVOID);
BOOL(WINAPI* UnmapViewOfFilePtr)(LPCVOID);

// Windows mappings need to be on 64K boundaries, due to Alpha legacy.
size_t MemArena::roundup(size_t x) {
	int gran = sysInfo.dwAllocationGranularity ? sysInfo.dwAllocationGranularity : 0x10000;
	return (x + gran - 1) & ~(gran - 1);
}

bool MemArena::GrabMemSpace(size_t size) {
	hMemoryMapping = CreateFileMappingWPtr(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)(size), NULL);
	GetSystemInfo(&sysInfo);
	return true;
}

void MemArena::ReleaseSpace() {
	CloseHandle(hMemoryMapping);
	hMemoryMapping = 0;
}

void *MemArena::CreateView(s64 offset, size_t size, void *viewbase) {
	size = roundup(size);
	void* ptr = MapViewOfFileExPtr(hMemoryMapping, FILE_MAP_ALL_ACCESS, 0, (DWORD)((u64)offset), size, viewbase);

	return ptr;
}

void MemArena::ReleaseView(void* view, size_t size) {
	UnmapViewOfFilePtr(view);
}

bool MemArena::NeedsProbing() {
	return true;
}

u8* MemArena::Find4GBBase() {
	// Now, create views in high memory where there's plenty of space.
	return nullptr;
}

#endif // _WIN32

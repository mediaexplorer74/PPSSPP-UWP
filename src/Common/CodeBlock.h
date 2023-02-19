// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include <cstddef>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/MemoryUtil.h"
#include "Core/Config.h"

// Everything that needs to generate code should inherit from this.
// You get memory management for free, plus, you can use all emitter functions without
// having to prefix them with gen-> or something similar.
// Example implementation:
// class JIT : public CodeBlock<ARMXEmitter>, public JitInterface {}

class CodeBlockCommon {
public:
	CodeBlockCommon() {}
	virtual ~CodeBlockCommon() {}

	bool IsInSpace(const u8 *ptr) const {
		return (ptr >= region) && (ptr < (region + region_size));
	}

	virtual const u8 *GetCodePtr() const = 0;

	u8 *GetBasePtr() {
		return region;
	}

	size_t GetOffset(const u8 *ptr) const {
		return ptr - region;
	}

	virtual const u8 *GetCodePtrFromWritablePtr(u8 *ptr) = 0;
	virtual u8 *GetWritablePtrFromCodePtr(const u8 *ptr) = 0;

protected:
	virtual void SetCodePtr(u8 *ptr) = 0;

	// Note: this should be the readable/executable side if writable is a different pointer.
	u8 *region = nullptr;
	size_t region_size = 0;
};

template<class T> class CodeBlock : public CodeBlockCommon, public T {
private:
	CodeBlock(const CodeBlock &) = delete;
	void operator=(const CodeBlock &) = delete;

	// A privately used function to set the executable RAM space to something invalid.
	// For debugging usefulness it should be used to set the RAM to a host specific breakpoint instruction
	virtual void PoisonMemory(int offset) = 0;

public:
	CodeBlock() {}
	~CodeBlock() {
		if (region)
			FreeCodeSpace();
	}

	// Call this before you generate any code.
	void AllocCodeSpace(int size) {
		region_size = size;
		// The protection will be set to RW if PlatformIsWXExclusive.
		region = (u8 *)AllocateExecutableMemory(region_size);
		writableRegion = region;
		T::SetCodePointer(region, writableRegion);
	}

	// Always clear code space with breakpoints, so that if someone accidentally executes
	// uninitialized, it just breaks into the debugger.
	void ClearCodeSpace(int offset) {
		if (!region) {
			return;
		}
		PoisonMemory(offset);
		ResetCodePtr(offset);
	}

	

	// Call this when shutting down. Don't rely on the destructor, even though it'll do the job.
	void FreeCodeSpace() {
		ProtectMemoryPages(region, region_size, MEM_PROT_READ | MEM_PROT_WRITE);
		FreeMemoryPages(region, region_size);
		region = nullptr;
		writableRegion = nullptr;
		region_size = 0;
	}

	const u8 *GetCodePtr() const override {
		return T::GetCodePointer();
	}

	void ResetCodePtr(size_t offset) {
		T::SetCodePointer(region + offset, writableRegion + offset);
	}

	size_t GetSpaceLeft() const {
		return region_size - (T::GetCodePointer() - region);
	}

	const u8 *GetCodePtrFromWritablePtr(u8 *ptr) override {
		// So we can adjust region to writable space.  Might be zero.
		ptrdiff_t writable = T::GetWritableCodePtr() - T::GetCodePointer();
		return ptr - writable;
	}

	u8 *GetWritablePtrFromCodePtr(const u8 *ptr) override {
		// So we can adjust region to writable space.  Might be zero.
		ptrdiff_t writable = T::GetWritableCodePtr() - T::GetCodePointer();
		return (u8 *)ptr + writable;
	}

protected:
	void SetCodePtr(u8 *ptr) override {
		T::SetCodePointer(ptr, GetWritablePtrFromCodePtr(ptr));
	}

private:
	// Note: this is a readable pointer.
	const uint8_t *writeStart_ = nullptr;
	uint8_t *writableRegion = nullptr;
	size_t writeEstimated_ = 0;
};


/*
  zip_source_file_win32_utf16.c -- source for Windows file opened by UTF-16 name
  Copyright (C) 1999-2020 Dieter Baron and Thomas Klausner

  This file is part of libzip, a library to manipulate ZIP archives.
  The authors can be contacted at <libzip@nih.at>

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
  1. Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in
  the documentation and/or other materials provided with the
  distribution.
  3. The names of the authors may not be used to endorse or promote
  products derived from this software without specific prior
  written permission.

  THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS
  OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#ifdef MS_UWP
#ifdef _M_ARM
#define _ARM_
#else
#define _AMD64_
#endif
#if !defined(LEGACY_SUPPORT)
#include <fileapifromapp.h>
#else
#include <fileapi.h>
#endif
#if !defined(NO_STORAGE_MANAGER) && !defined(__LIBRETRO__)
#include "UWP/UWPHelpers/UWP2C.h"
#endif
#endif

#include "zip_source_file_win32.h"

static char *utf16_allocate_tempname(const char *name, size_t extra_chars, size_t *lengthp);
static HANDLE __stdcall utf16_create_file(const char *name, DWORD access, DWORD share_mode, PSECURITY_ATTRIBUTES security_attributes, DWORD creation_disposition, DWORD file_attributes, HANDLE template_file);
static void utf16_make_tempname(char *buf, size_t len, const char *name, zip_uint32_t i);
static char *utf16_strdup(const char *string);

#ifdef MS_UWP && !defined(NO_STORAGE_MANAGER) && !defined(__LIBRETRO__)
static BOOL GetFileAttr(const void* name, GET_FILEEX_INFO_LEVELS info_level, void* lpFileInformation);
static BOOL __stdcall
GetFileAttr(const void* name, GET_FILEEX_INFO_LEVELS info_level, void* lpFileInformation) {
	// Try API first
	if(!GetFileAttributesExW(name, info_level, lpFileInformation)){
	  return GetFileAttributesUWP(name, lpFileInformation);
	}
	return 1;
}

static BOOL DelFile(const void* name);
static BOOL __stdcall
DelFile(const void* name) {
	return DeleteFileUWP(name);
}

zip_win32_file_operations_t ops_utf16 = {
	utf16_allocate_tempname,
	utf16_create_file, // Will invoke UWP Storage manager (If needed)
	DelFile, // Will invoke UWP Storage manager
	GetFileAttributesW,
	GetFileAttr, // Will invoke UWP Storage manager
	utf16_make_tempname,
	MoveFileExW,
	SetFileAttributesW,
	utf16_strdup
};
#else
zip_win32_file_operations_t ops_utf16 = {
	utf16_allocate_tempname,
	utf16_create_file,
	DeleteFileW,
	GetFileAttributesW,
	GetFileAttributesExW,
	utf16_make_tempname,
	MoveFileExW,
	SetFileAttributesW,
	utf16_strdup
};
#endif

ZIP_EXTERN zip_source_t *
zip_source_win32w(zip_t *za, const wchar_t *fname, zip_uint64_t start, zip_int64_t len) {
    if (za == NULL)
    return NULL;

    return zip_source_win32w_create(fname, start, len, &za->error);
}


ZIP_EXTERN zip_source_t *
zip_source_win32w_create(const wchar_t *fname, zip_uint64_t start, zip_int64_t length, zip_error_t *error) {
    if (fname == NULL || length < -1) {
    zip_error_set(error, ZIP_ER_INVAL, 0);
    return NULL;
    }


    return zip_source_file_common_new((const char *)fname, NULL, start, length, NULL, &_zip_source_file_win32_named_ops, &ops_utf16, error);
}


static char *
utf16_allocate_tempname(const char *name, size_t extra_chars, size_t *lengthp) {
    *lengthp = wcslen((const wchar_t *)name) + extra_chars;
    return (char *)malloc(*lengthp * sizeof(wchar_t));
}


static HANDLE __stdcall
utf16_create_file(const char *name, DWORD access, DWORD share_mode, PSECURITY_ATTRIBUTES security_attributes, DWORD creation_disposition, DWORD file_attributes, HANDLE template_file) {
#ifdef MS_UWP
#if 0
    CREATEFILE2_EXTENDED_PARAMETERS extParams = {0};
    extParams.dwFileAttributes = file_attributes;
    extParams.dwFileFlags = FILE_FLAG_RANDOM_ACCESS;
    extParams.dwSecurityQosFlags = SECURITY_ANONYMOUS;
    extParams.dwSize = sizeof(extParams);
    extParams.hTemplateFile = template_file;
    extParams.lpSecurityAttributes = security_attributes;

    return CreateFile2((const wchar_t *)name, access, share_mode, creation_disposition, &extParams);
#else
#if !defined(LEGACY_SUPPORT)
    HANDLE h = CreateFile2FromAppW((const wchar_t *)name, access, share_mode, creation_disposition, NULL);
#else
    HANDLE h = CreateFile2((const wchar_t*)name, access, share_mode, creation_disposition, NULL);
#endif
#if !defined(NO_STORAGE_MANAGER) && !defined(__LIBRETRO__)
	if (h == INVALID_HANDLE_VALUE) {
		h = CreateFileUWP(name, (int)access, (int)share_mode, (int)creation_disposition);
	}
#endif
    return h;
#endif
#else
    return CreateFileW((const wchar_t *)name, access, share_mode, security_attributes, creation_disposition, file_attributes, template_file);
#endif
}


static void
utf16_make_tempname(char *buf, size_t len, const char *name, zip_uint32_t i) {
    _snwprintf((wchar_t *)buf, len, L"%s.%08x", (const wchar_t *)name, i);
}


static char *
utf16_strdup(const char *string) {
    return (char *)_wcsdup((const wchar_t *)string);
}

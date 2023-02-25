#pragma once

#include <Windows.h>
#include <string>

HMODULE LoadLibraryGus(std::wstring Filename);
FARPROC GetFromKernel(std::wstring process);
std::wstring Str(const char* char_array);
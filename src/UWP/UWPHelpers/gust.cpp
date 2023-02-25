#include "gust.h"

#undef WINAPI_PARTITION_SYSTEM
#define WINAPI_PARTITION_SYSTEM 1
#include <winternl.h>
#undef WINAPI_PARTITION_SYSTEM
#define WINAPI_PARTITION_SYSTEM 0

// Redefine PEB structures. The structure definitions in winternl.h are incomplete.
typedef struct _MY_PEB_LDR_DATA {
    ULONG Length;
    BOOL Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} MY_PEB_LDR_DATA, * PMY_PEB_LDR_DATA;

typedef struct _MY_LDR_DATA_TABLE_ENTRY
{
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} MY_LDR_DATA_TABLE_ENTRY, * PMY_LDR_DATA_TABLE_ENTRY;


#include <algorithm>
#include <string>

std::wstring Str(UNICODE_STRING US) {
    wchar_t* str = (wchar_t*)malloc(US.Length + sizeof(wchar_t));
    memcpy(str, US.Buffer, US.Length);
    str[US.Length / sizeof(wchar_t)] = 0;
    return str;
}

std::wstring Str(const char* char_array) {
    std::string s_str = std::string(char_array);
    std::wstring wid_str = std::wstring(s_str.begin(), s_str.end());
    const wchar_t* w_char = wid_str.c_str();
    return w_char;
}

std::wstring ToLower(std::wstring str) {
    std::wstring wid_str = str;
    std::transform(wid_str.begin(), wid_str.end(), wid_str.begin(), ::tolower);
    return wid_str;
}

PEB* NtCurrentPeb() {
#ifdef _M_X64
	return (PEB*)(__readgsqword(0x60));
#elif _M_IX86
	return (PEB*)(__readfsdword(0x30));
#elif _M_ARM
	return *(PEB**)(_MoveFromCoprocessor(15, 0, 13, 0, 2) + 0x30);
#elif _M_ARM64
	return *(PEB**)(__getReg(18) + 0x60); // TEB in x18
#elif _M_IA64
	return *(PEB**)(_rdteb() + 0x60);
#elif _M_ALPHA
	return *(PEB**)(_rdteb() + 0x30);
#elif _M_MIPS
	return *(PEB**)((*(char**)(0x7ffff030)) + 0x30);
#elif _M_PPC
	// winnt.h of the period uses __builtin_get_gpr13() or __gregister_get(13) depending on _MSC_VER
	return *(PEB**)(__gregister_get(13) + 0x30);
#else
#error "This architecture is currently unsupported"
#endif
};

FARPROC GetProcAddress(std::wstring dll, std::wstring func) {
    dll = ToLower(dll);
    func = ToLower(func);
    auto Ldr = (PMY_PEB_LDR_DATA)(NtCurrentPeb()->Ldr);
    auto NextModule = Ldr->InLoadOrderModuleList.Flink;
    auto TableEntry = (PMY_LDR_DATA_TABLE_ENTRY)NextModule;
    while (TableEntry->DllBase != NULL) {
        PVOID base = TableEntry->DllBase;
        std::wstring dllName = ToLower(Str(TableEntry->BaseDllName));
        auto PE = (PIMAGE_NT_HEADERS)((ULONG_PTR)base + ((PIMAGE_DOS_HEADER)base)->e_lfanew);
        auto exportdirRVA = PE->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        TableEntry = (PMY_LDR_DATA_TABLE_ENTRY)TableEntry->InLoadOrderLinks.Flink;

        if (exportdirRVA == 0) continue;
        if (dllName != dll) continue;

        auto Exports = (PIMAGE_EXPORT_DIRECTORY)((ULONG_PTR)base + exportdirRVA);
        auto Names = (PDWORD)((PCHAR)base + Exports->AddressOfNames);
        auto Ordinals = (PUSHORT)((ULONG_PTR)base + Exports->AddressOfNameOrdinals);
        auto Functions = (PDWORD)((ULONG_PTR)base + Exports->AddressOfFunctions);

        for (int iterator = 0; iterator < Exports->NumberOfNames; iterator++) {
            std::wstring funcName = ToLower(Str((PCSTR)(Names[iterator] + (ULONG_PTR)base)));

            if (funcName != func) continue;

            USHORT ordTblIndex = Ordinals[iterator];
            return (FARPROC)((ULONG_PTR)base + Functions[ordTblIndex]);
        }
    }
    return NULL;
}

HMODULE LoadLibraryGus(std::wstring Filename) {
    typedef HMODULE(WINAPI* funcLoadLibraryW)(const wchar_t* lpFileName);

    funcLoadLibraryW LoadLibraryW = (funcLoadLibraryW)GetProcAddress(L"kernelbase.dll", L"LoadLibraryW");
    if (LoadLibraryW == NULL) return NULL;
    return LoadLibraryW(Filename.c_str());
}

FARPROC GetFromKernel(std::wstring process) {
    return GetProcAddress(L"kernelbase.dll", process);
}

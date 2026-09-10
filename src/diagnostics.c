#include <windows.h>
#include <stdio.h>
#include "diagnostics.h"
static wchar_t logPath[MAX_PATH];
static LONG operation;
void diagnostics_phase(long phase) { InterlockedExchange(&operation,phase); }
static LONG WINAPI failed(EXCEPTION_POINTERS *error) {
    HMODULE module=NULL; wchar_t modulePath[MAX_PATH]=L"unknown";
    ULONG_PTR address=(ULONG_PTR)error->ExceptionRecord->ExceptionAddress;
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)address,&module)) GetModuleFileNameW(module,modulePath,MAX_PATH);
    const wchar_t *name=wcsrchr(modulePath,L'\\'); name=name?name+1:modulePath;
    char line[512]; SYSTEMTIME time; GetSystemTime(&time);
    int n=snprintf(line,sizeof(line),"%04u-%02u-%02uT%02u:%02u:%02uZ exception=0x%08lx module=%ls offset=0x%llx operation=%ld\r\n",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,
        error->ExceptionRecord->ExceptionCode,name,(unsigned long long)(address-(ULONG_PTR)module),
        InterlockedCompareExchange(&operation,0,0));
    HANDLE file=CreateFileW(logPath,FILE_APPEND_DATA,FILE_SHARE_READ,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file!=INVALID_HANDLE_VALUE) {
        DWORD written; if(n>0) WriteFile(file,line,(DWORD)(n<(int)sizeof(line)?n:(int)sizeof(line)-1),&written,NULL);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void diagnostics_start(void) {
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",logPath,MAX_PATH);
    if(!n || n>MAX_PATH-50) return;
    wcscat(logPath,L"\\DesktopsHelper"); CreateDirectoryW(logPath,NULL);
    wcscat(logPath,L"\\crash.log"); SetUnhandledExceptionFilter(failed);
}

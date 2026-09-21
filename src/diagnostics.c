#include <windows.h>
#include <stdio.h>
#include "diagnostics.h"
static wchar_t logPath[MAX_PATH];
static wchar_t lifecyclePath[MAX_PATH], markerPath[MAX_PATH];
static BOOL ownsMarker;
static LONG operation;
static BOOL append(const wchar_t *path,const char *line,int length) {
    if(!*path || length<=0)return FALSE;
    HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return FALSE;
    DWORD written=0;
    BOOL ok=WriteFile(file,line,(DWORD)length,&written,NULL) && written==(DWORD)length;
    if(ok)ok=FlushFileBuffers(file);
    CloseHandle(file);return ok;
}
int diagnostics_event(const char *event,const char *reason,unsigned long code) {
    char line[768];SYSTEMTIME time;GetSystemTime(&time);
    int n=snprintf(line,sizeof(line),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ pid=%lu event=%s reason=%s code=%lu operation=%ld\r\n",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,
        GetCurrentProcessId(),event,reason,code,InterlockedCompareExchange(&operation,0,0));
    return append(lifecyclePath,line,n>=(int)sizeof(line)?(int)sizeof(line)-1:n);
}
void diagnostics_owner(void) {
    if(!*markerPath)return;
    HANDLE file=CreateFileW(markerPath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE){diagnostics_event("logging-error","open-marker",GetLastError());return;}
    char previous[160]={0};DWORD count=0;
    if(ReadFile(file,previous,sizeof(previous)-1,&count,NULL) && count)
        diagnostics_event("previous-exit-unconfirmed",previous,0);
    SYSTEMTIME time;GetSystemTime(&time);
    char marker[160];int n=snprintf(marker,sizeof(marker),"pid-%lu-started-%04u%02u%02uT%02u%02u%02uZ",
        GetCurrentProcessId(),time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
    SetFilePointer(file,0,NULL,FILE_BEGIN);
    BOOL written=WriteFile(file,marker,(DWORD)n,&count,NULL) && count==(DWORD)n;
    if(written)written=SetEndOfFile(file) && FlushFileBuffers(file);
    DWORD error=GetLastError();CloseHandle(file);ownsMarker=written;
    if(!written)diagnostics_event("logging-error","write-marker",error);
}
int diagnostics_finish(const char *reason,int code) {
    BOOL recorded=diagnostics_event("exit",reason,(unsigned long)code);
    if(ownsMarker && recorded){
        if(!DeleteFileW(markerPath))diagnostics_event("logging-error","remove-marker",GetLastError());
        ownsMarker=FALSE;
    }
    return code;
}
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
    append(logPath,line,n<(int)sizeof(line)?n:(int)sizeof(line)-1);
    diagnostics_event("crash", "unhandled-exception",error->ExceptionRecord->ExceptionCode);
    return EXCEPTION_CONTINUE_SEARCH;
}
void diagnostics_start(void) {
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",logPath,MAX_PATH);
    if(!n || n>MAX_PATH-50) return;
    wcscat(logPath,L"\\DesktopsHelper"); CreateDirectoryW(logPath,NULL);
    swprintf(lifecyclePath,MAX_PATH,L"%ls\\lifecycle.log",logPath);
    DWORD session=0;ProcessIdToSessionId(GetCurrentProcessId(),&session);
    swprintf(markerPath,MAX_PATH,L"%ls\\active-%lu.txt",logPath,session);
    wcscat(logPath,L"\\crash.log"); SetUnhandledExceptionFilter(failed);
    BOOL inJob=FALSE;IsProcessInJob(GetCurrentProcess(),NULL,&inJob);
    char context[128];snprintf(context,sizeof(context),"session-%lu-job-%d",session,(int)inJob);
    diagnostics_event("start",context,0);
}

#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <limits.h>
#include "native_taskbar.h"
#include "taskbar_protocol.h"
#include "service.h"

// Only the helper UI thread accesses this coordinator. The controller is
// short-lived; neither highlighting nor owner lifetime uses an idle timer.
static HWND nativeWidget,nativeControl;
static BOOL nativeMode;
static UINT nativeRequest,nativeState;
static HANDLE nativeLauncher,nativeJob;
static ULONGLONG attachDeadline;
static DWORD nativeShellPid;
static unsigned lastPublished=UINT_MAX;

static BOOL validNativeControl(HWND endpoint) {
    wchar_t name[96];DWORD endpointPid=0,shellPid=0;
    HWND shell=FindWindowW(L"Shell_TrayWnd",NULL);
    if(!endpoint || !shell || !GetClassNameW(endpoint,name,ARRAYSIZE(name)) || wcscmp(name,DH_TASKBAR_CLASS))return FALSE;
    GetWindowThreadProcessId(endpoint,&endpointPid);GetWindowThreadProcessId(shell,&shellPid);
    return endpointPid && endpointPid==shellPid && GetPropW(endpoint,DH_TASKBAR_MOUNTED) &&
        !GetPropW(endpoint,DH_TASKBAR_CANCELLED) &&
        (DWORD)(UINT_PTR)GetPropW(endpoint,DH_TASKBAR_OWNER)==GetCurrentProcessId();
}
void native_taskbar_publish(void) {
    if(!validNativeControl(nativeControl)){nativeControl=NULL;return;}
    ServiceState state=service_state();
    unsigned value=state.ready?(state.current>=0 && state.current<5?(unsigned)state.current+1:6):0;
    if(value!=lastPublished){
        if(PostMessageW(nativeControl,nativeState,value,(LPARAM)nativeWidget))lastPublished=value;
    }
}
void native_taskbar_tick(void) {
    HWND candidate=FindWindowExW(HWND_MESSAGE,NULL,DH_TASKBAR_CLASS,NULL);
    if(validNativeControl(candidate)){
        nativeControl=candidate;lastPublished=UINT_MAX;native_taskbar_publish();ShowWindow(nativeWidget,SW_HIDE);
        KillTimer(nativeWidget,NATIVE_ATTACH_TIMER);
        if(nativeLauncher){CloseHandle(nativeLauncher);nativeLauncher=NULL;}
        return;
    }
    if(GetTickCount64()>=attachDeadline){
        KillTimer(nativeWidget,NATIVE_ATTACH_TIMER);
        if(nativeJob)TerminateJobObject(nativeJob,ERROR_TIMEOUT);
        if(nativeLauncher){CloseHandle(nativeLauncher);nativeLauncher=NULL;}
        return;
    }
    if(nativeLauncher){if(WaitForSingleObject(nativeLauncher,0)==WAIT_TIMEOUT)return;CloseHandle(nativeLauncher);nativeLauncher=NULL;}
    // Do not start a five-second attempt without room in the overall deadline.
    if(attachDeadline-GetTickCount64()<5500){KillTimer(nativeWidget,NATIVE_ATTACH_TIMER);return;}
    wchar_t executable[MAX_PATH],command[MAX_PATH+32];
    DWORD length=GetModuleFileNameW(NULL,executable,ARRAYSIZE(executable));
    if(!length || length>=ARRAYSIZE(executable))return;
    wchar_t* slash=wcsrchr(executable,L'\\');if(!slash || (size_t)(slash-executable)+22>=ARRAYSIZE(executable))return;
    wcscpy(slash+1,L"TaskbarPrototype.exe");
    swprintf(command,ARRAYSIZE(command),L"\"%ls\" attach",executable);
    STARTUPINFOW startup={.cb=sizeof(startup)};PROCESS_INFORMATION child={0};
    if(nativeJob && CreateProcessW(executable,command,NULL,NULL,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,NULL,NULL,&startup,&child)){
        // The job owns only controllers we create, never Explorer or the helper.
        if(AssignProcessToJobObject(nativeJob,child.hProcess) && ResumeThread(child.hThread)!=(DWORD)-1)
            nativeLauncher=child.hProcess;
        else {TerminateProcess(child.hProcess,ERROR_CANCELLED);CloseHandle(child.hProcess);}
        CloseHandle(child.hThread);
    }
}
void native_taskbar_begin(void){
    if(!nativeMode)return;
    nativeControl=NULL;attachDeadline=GetTickCount64()+15000;SetTimer(nativeWidget,NATIVE_ATTACH_TIMER,1000,NULL);native_taskbar_tick();
}
BOOL native_taskbar_restart_for_shell(void) {
    DWORD shellPid=0;GetWindowThreadProcessId(FindWindowW(L"Shell_TrayWnd",NULL),&shellPid);
    if(!nativeMode || !shellPid || shellPid==nativeShellPid)return FALSE;
    wchar_t executable[MAX_PATH],command[MAX_PATH+96];
    DWORD length=GetModuleFileNameW(NULL,executable,ARRAYSIZE(executable));
    if(!length || length>=ARRAYSIZE(executable))return FALSE;
    swprintf(command,ARRAYSIZE(command),L"\"%ls\" --native-taskbar --wait-for-pid=%lu",executable,GetCurrentProcessId());
    STARTUPINFOW startup={.cb=sizeof(startup)};PROCESS_INFORMATION child={0};
    if(!CreateProcessW(executable,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&child))return FALSE;
    CloseHandle(child.hThread);CloseHandle(child.hProcess);return TRUE;
}


void native_taskbar_init(HWND window,BOOL enabled) {
    nativeWidget=window;nativeMode=enabled;
    GetWindowThreadProcessId(FindWindowW(L"Shell_TrayWnd",NULL),&nativeShellPid);
    nativeRequest=RegisterWindowMessageW(DH_TASKBAR_REQUEST);
    nativeState=RegisterWindowMessageW(DH_TASKBAR_STATE);
    if(enabled){
        nativeJob=CreateJobObjectW(NULL,NULL);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit={0};
        limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(nativeJob && !SetInformationJobObject(nativeJob,JobObjectExtendedLimitInformation,&limit,sizeof(limit))){
            CloseHandle(nativeJob);nativeJob=NULL;
        }
    }
}
BOOL native_taskbar_active(void) {
    if(validNativeControl(nativeControl))return TRUE;
    nativeControl=NULL;return FALSE;
}
BOOL native_taskbar_handle(UINT message,WPARAM wp,LPARAM lp) {
    if(!nativeRequest || message!=nativeRequest)return FALSE;
    HWND endpoint=(HWND)lp;
    if(wp==DH_TASKBAR_DETACH && endpoint==nativeControl){nativeControl=NULL;return TRUE;}
    if(!nativeMode || wp>5 || !validNativeControl(endpoint))return TRUE;
    if(nativeControl!=endpoint || wp==DH_TASKBAR_SUBSCRIBE)lastPublished=UINT_MAX;
    nativeControl=endpoint;
    if(wp>=1)service_submit((int)wp-1,FALSE,NULL);
    native_taskbar_publish();
    return TRUE;
}
void native_taskbar_stop(void) {
    native_taskbar_detach();
    if(nativeJob){CloseHandle(nativeJob);nativeJob=NULL;}
}
void native_taskbar_detach(void) {
    KillTimer(nativeWidget,NATIVE_ATTACH_TIMER);
    if(nativeJob)TerminateJobObject(nativeJob,ERROR_CANCELLED);
    if(nativeLauncher){CloseHandle(nativeLauncher);nativeLauncher=NULL;}
    if(validNativeControl(nativeControl)){
        // A queued removal must not be adopted by a subsequent attachment attempt.
        SetPropW(nativeControl,DH_TASKBAR_CANCELLED,(HANDLE)1);
        PostMessageW(nativeControl,DH_TASKBAR_REMOVE,0,0);
    }
    nativeControl=NULL;
}

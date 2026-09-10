#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include "../src/adapter.h"
static LRESULT CALLBACK test_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CHAR && wp == 'x') SetPropW(hwnd,L"Typed",(HANDLE)1);
    if (msg == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void pump(DWORD milliseconds) {
    ULONGLONG end=GetTickCount64()+milliseconds;
    do { MSG m; while(PeekMessageW(&m,NULL,0,0,PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } Sleep(10); } while(GetTickCount64()<end);
}
static int chord(int number, BOOL shift) {
    INPUT keys[6] = {0}; int n=0;
#define KEY(v,f) do { keys[n].type=INPUT_KEYBOARD; keys[n].ki.wVk=(v); keys[n++].ki.dwFlags=(f); } while(0)
    KEY(VK_LWIN,0); if(shift) KEY(VK_LSHIFT,0);
    KEY('1'+number,0); KEY('1'+number,KEYEVENTF_KEYUP);
    if(shift) { KEY(VK_LSHIFT,KEYEVENTF_KEYUP); }
    KEY(VK_LWIN,KEYEVENTF_KEYUP);
#undef KEY
    return SendInput(n,keys,sizeof(INPUT))==(UINT)n;
}
static DWORD childPid;
static HWND childWindow;
static BOOL CALLBACK find_child(HWND hwnd, LPARAM unused) {
    (void)unused; DWORD pid=0; GetWindowThreadProcessId(hwnd,&pid);
    if(pid==childPid && IsWindowVisible(hwnd)) { childWindow=hwnd; return FALSE; }
    return TRUE;
}
int main(int argc, char **argv) {
    (void)argv;
    if(argc>1) {
        WNDCLASSW wc={.lpfnWndProc=test_proc,.hInstance=GetModuleHandleW(NULL),.lpszClassName=L"DesktopsHelper.FocusTest"};
        RegisterClassW(&wc);
        HWND h=CreateWindowW(wc.lpszClassName,L"Destination focus test",WS_OVERLAPPEDWINDOW,150,150,400,200,NULL,NULL,wc.hInstance,NULL);
        ShowWindow(h,SW_SHOWNOACTIVATE);
        MSG msg; while(GetMessageW(&msg,NULL,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        return 0;
    }
    HWND helper=FindWindowW(L"DesktopsHelper.Widget",NULL);
    if(!helper || !GetPropW(helper,L"DesktopsHelper.Ready")) { fprintf(stderr,"Helper not ready (window=%p, stage=%lld)\n",(void*)helper,(long long)(INT_PTR)GetPropW(helper,L"DesktopsHelper.StartupStage")); return 1; }
    Adapter a={0}; if(!loadAdapter(&a)) return 1;
    int original=a.current(), failed=0;
    if(original < 0) return 1;
    HWND foreground=GetForegroundWindow();
    WNDCLASSW wc={.lpfnWndProc=test_proc,.hInstance=GetModuleHandleW(NULL),.lpszClassName=L"DesktopsHelper.ShortcutTest"};
    RegisterClassW(&wc);
    HWND h=CreateWindowW(wc.lpszClassName,L"Desktops Helper shortcut test",WS_OVERLAPPEDWINDOW,100,100,400,200,NULL,NULL,wc.hInstance,NULL);
    ShowWindow(h,SW_SHOW); pump(200);
    HWND destinations[5]={0};
    PROCESS_INFORMATION children[5]={0};
    for(int i=0;i<5;++i) {
        while(a.count()<=i) { if(a.create()<0) return 1; }
        char path[MAX_PATH],command[MAX_PATH+32]; GetModuleFileNameA(NULL,path,MAX_PATH);
        snprintf(command,sizeof(command),"\"%s\" --focus-window",path);
        STARTUPINFOA si={.cb=sizeof(si)};
        if(!CreateProcessA(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&children[i])) return 1;
        childPid=children[i].dwProcessId; childWindow=NULL;
        ULONGLONG end=GetTickCount64()+3000;
        do { EnumWindows(find_child,0); pump(10); } while(!childWindow && GetTickCount64()<end);
        destinations[i]=childWindow;
        if(a.move(destinations[i],i)<0) failed++;
    }
    for(int i=0;i<5;++i) {
        // Start on another desktop with a known foreground window.
        a.go((i+4)%5); a.move(h,(i+4)%5); pump(200);
        SetForegroundWindow(h); pump(100);
        if(!chord(i,FALSE)) failed++;
        ULONGLONG deadline=GetTickCount64()+3000;
        while(((int)(INT_PTR)GetPropW(helper,L"DesktopsHelper.Current")!=i+1 || a.current()!=i) && GetTickCount64()<deadline) pump(10);
        if(a.current()!=i || (int)(INT_PTR)GetPropW(helper,L"DesktopsHelper.Current")!=i+1) {
            fprintf(stderr,"Shortcut switch mismatch: target=%d actual=%d indicator=%lld\n",i,a.current(),(long long)(INT_PTR)GetPropW(helper,L"DesktopsHelper.Current"));failed++;
        }
        deadline=GetTickCount64()+2000;
        while(GetForegroundWindow()!=destinations[i] && GetTickCount64()<deadline) pump(10);
        if(GetForegroundWindow()!=destinations[i]) {
            HWND actual=GetForegroundWindow(); wchar_t cls[128]={0}; GetClassNameW(actual,cls,128);
            fprintf(stderr,"Destination focus mismatch: desktop=%d actual=%p expected=%p actualDesktop=%d class=%ls\n",i,(void*)actual,(void*)destinations[i],a.windowDesktop(actual),cls); failed++;
        } else {
            INPUT keys[2]={0}; keys[0].type=keys[1].type=INPUT_KEYBOARD;
            keys[0].ki.wVk=keys[1].ki.wVk='X'; keys[1].ki.dwFlags=KEYEVENTF_KEYUP;
            if(SendInput(2,keys,sizeof(INPUT))!=2) failed++;
            pump(100);
            if(!GetPropW(destinations[i],L"Typed")) { fputs("Typing missed destination window\n",stderr); failed++; }
        }
        if(a.move(h,i)<0) failed++;
        pump(150); SetForegroundWindow(h); pump(100);
        if(GetForegroundWindow()!=h) { fputs("Test-window focus unavailable\n",stderr);failed++; continue; }
        int dest=(i+1)%5;
        if(!chord(dest,TRUE)) failed++;
        deadline=GetTickCount64()+3000;
        while(a.windowDesktop(h)!=dest && GetTickCount64()<deadline) pump(10);
        if(a.windowDesktop(h)!=dest || a.current()!=i) {
            fprintf(stderr,"Shortcut move mismatch: target=%d window=%d current=%d expectedCurrent=%d\n",dest,a.windowDesktop(h),a.current(),i);failed++;
        }
        HWND fg=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(fg,&pid);
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(process) {
            wchar_t path[MAX_PATH];DWORD length=MAX_PATH;
            if(QueryFullProcessImageNameW(process,0,path,&length) && wcsstr(path,L"StartMenuExperienceHost.exe"))
                fputs("Unexpected Start menu foreground after shortcut\n",stderr);
            CloseHandle(process);
        }
    }
    for(int i=0;i<5;++i) {
        PostMessageW(destinations[i],WM_CLOSE,0,0);
        WaitForSingleObject(children[i].hProcess,2000);
        CloseHandle(children[i].hProcess); CloseHandle(children[i].hThread);
    }
    a.go(original); pump(400); DestroyWindow(h); SetForegroundWindow(foreground); pump(300);
    printf("{\"shortcutsTested\":10,\"failures\":%d,\"returnedToDesktop\":%d}\n",failed,original+1);
    return failed?1:0;
}

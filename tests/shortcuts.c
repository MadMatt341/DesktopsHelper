#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include "../src/adapter.h"
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
int main(void) {
    HWND helper=FindWindowW(L"DesktopsHelper.Widget",NULL);
    if(!helper || !GetPropW(helper,L"DesktopsHelper.Ready")) { fprintf(stderr,"Helper not ready (window=%p, stage=%lld)\n",(void*)helper,(long long)(INT_PTR)GetPropW(helper,L"DesktopsHelper.StartupStage")); return 1; }
    Adapter a={0}; if(!loadAdapter(&a)) return 1;
    int original=a.current(), failed=0;
    if(original < 0) return 1;
    HWND foreground=GetForegroundWindow();
    WNDCLASSW wc={.lpfnWndProc=DefWindowProcW,.hInstance=GetModuleHandleW(NULL),.lpszClassName=L"DesktopsHelper.ShortcutTest"};
    RegisterClassW(&wc);
    HWND h=CreateWindowW(wc.lpszClassName,L"Desktops Helper shortcut test",WS_OVERLAPPEDWINDOW,100,100,400,200,NULL,NULL,wc.hInstance,NULL);
    ShowWindow(h,SW_SHOW); pump(200);
    for(int i=0;i<5;++i) {
        if(!chord(i,FALSE)) failed++;
        ULONGLONG deadline=GetTickCount64()+3000;
        while(((int)(INT_PTR)GetPropW(helper,L"DesktopsHelper.Current")!=i+1 || a.current()!=i) && GetTickCount64()<deadline) pump(10);
        if(a.current()!=i || (int)(INT_PTR)GetPropW(helper,L"DesktopsHelper.Current")!=i+1) {
            fprintf(stderr,"Shortcut switch mismatch: target=%d actual=%d indicator=%lld\n",i,a.current(),(long long)(INT_PTR)GetPropW(helper,L"DesktopsHelper.Current"));failed++;
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
    a.go(original); pump(400); DestroyWindow(h); SetForegroundWindow(foreground); pump(300);
    printf("{\"shortcutsTested\":10,\"failures\":%d,\"returnedToDesktop\":%d}\n",failed,original+1);
    return failed?1:0;
}

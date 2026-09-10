#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
static void pump(void) {
    ULONGLONG until=GetTickCount64()+600;
    do {
        MSG msg;
        while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) {TranslateMessage(&msg); DispatchMessageW(&msg);}
        Sleep(10);
    } while(GetTickCount64()<until);
}
static int check(HWND widget, BOOL visible, const char *name) {
    pump();
    int ok=!!IsWindowVisible(widget)==!!visible;
    printf("%s: %s\n",name,ok ? "PASS" : "FAIL");
    return !ok;
}
int main(void) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND widget=FindWindowW(L"DesktopsHelper.Widget",NULL), previous=GetForegroundWindow();
    if(!widget) return 1;
    MONITORINFO mi={.cbSize=sizeof(mi)};
    if(!GetMonitorInfoW(MonitorFromWindow(widget,MONITOR_DEFAULTTOPRIMARY),&mi)) return 1;
    WNDCLASSW wc={.lpfnWndProc=DefWindowProcW,.hInstance=GetModuleHandleW(NULL),.lpszClassName=L"DesktopsHelper.FullscreenTest"};
    RegisterClassW(&wc);
    HWND w=CreateWindowW(wc.lpszClassName,L"Fullscreen regression",WS_OVERLAPPEDWINDOW,
        mi.rcWork.left+200,mi.rcWork.top+100,500,400,NULL,NULL,wc.hInstance,NULL);
    if(!w) return 1;
    ShowWindow(w,SW_SHOW); pump();
    DWORD foregroundThread=GetWindowThreadProcessId(GetForegroundWindow(),NULL);
    BOOL attached=AttachThreadInput(GetCurrentThreadId(),foregroundThread,TRUE);
    SetForegroundWindow(w);
    if(attached) AttachThreadInput(GetCurrentThreadId(),foregroundThread,FALSE);
    pump();
    if(GetForegroundWindow()!=w) {DestroyWindow(w); puts("Could not obtain test focus"); return 1;}
    int failures=check(widget,TRUE,"windowed");
    ShowWindow(w,SW_MAXIMIZE);
    failures+=check(widget,TRUE,"maximized");
    ShowWindow(w,SW_RESTORE);
    SetWindowLongPtrW(w,GWL_STYLE,WS_POPUP|WS_VISIBLE);
    SetWindowPos(w,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,
        mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);
    failures+=check(widget,FALSE,"fullscreen without focus change");
    SetWindowPos(w,NULL,mi.rcWork.left+200,mi.rcWork.top+100,500,400,SWP_NOZORDER);
    failures+=check(widget,TRUE,"exit fullscreen without focus change");
    SetWindowPos(w,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,
        mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,0);
    failures+=check(widget,FALSE,"reenter fullscreen");
    ShowWindow(w,SW_MINIMIZE);
    failures+=check(widget,TRUE,"minimize fullscreen");
    ShowWindow(w,SW_RESTORE); SetForegroundWindow(w);
    failures+=check(widget,FALSE,"restore fullscreen");
    DestroyWindow(w);
    if(previous) SetForegroundWindow(previous);
    failures+=check(widget,TRUE,"close fullscreen");
    return failures ? 1 : 0;
}

#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
int main(int argc, char **argv) {
    HWND h=FindWindowW(L"DesktopsHelper.Widget",NULL);
    if(!h) { puts("Widget missing"); return 1; }
    if (argc > 1 && strcmp(argv[1], "--exercise") == 0) {
        HWND taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
        if (!taskbar || !SetWindowPos(taskbar, HWND_TOPMOST, 0,0,0,0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) return 1;
        Sleep(500);
    }
    RECT r={0}; GetWindowRect(h,&r);
    DWORD cloaked=0; DwmGetWindowAttribute(h,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
    POINT p={(r.left+r.right)/2,(r.top+r.bottom)/2};
    HWND hit=WindowFromPoint(p); wchar_t cls[256]={0}; GetClassNameW(hit,cls,256);
    printf("visible=%d iconic=%d cloaked=%lu rect=%ld,%ld,%ld,%ld hitWidget=%d\n",IsWindowVisible(h),IsIconic(h),cloaked,r.left,r.top,r.right,r.bottom,hit==h);
    wprintf(L"coveringClass=%ls\n",cls);
    return IsWindowVisible(h) && !IsIconic(h) && !cloaked && hit==h ? 0 : 1;
}

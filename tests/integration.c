#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include "../src/adapter.h"
static int changes;
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_APP + 1) { ++changes; return 0; }
    return DefWindowProcW(h,m,w,l);
}
static void pump(DWORD milliseconds) {
    ULONGLONG end = GetTickCount64() + milliseconds;
    do {
        MSG msg; while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(10);
    } while (GetTickCount64() < end);
}
int main(int argc, char **argv) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    Adapter a = {0};
    if (!loadAdapter(&a)) { fprintf(stderr,"Adapter unavailable\n"); return 1; }
    int original = a.current(), count = a.count();
    if(original < 0 || count < 1) { fputs("Desktop service unavailable\n",stderr); return 1; }
    printf("{\"desktopCount\":%d,\"currentDesktop\":%d",count,original+1);
    if (argc < 2 || strcmp(argv[1],"--exercise")) { puts("}"); return 0; }
    WNDCLASSW wc = {.lpfnWndProc=proc,.hInstance=GetModuleHandleW(NULL),.lpszClassName=L"DesktopsHelper.Test"};
    RegisterClassW(&wc);
    HWND h = CreateWindowW(wc.lpszClassName,L"Desktops Helper integration test",WS_OVERLAPPEDWINDOW,
        100,100,400,200,NULL,NULL,wc.hInstance,NULL);
    ShowWindow(h,SW_SHOWNOACTIVATE); pump(200);
    int failed = 0;
    if (a.subscribe(h,WM_APP+1) < 0) failed++;
    LARGE_INTEGER freq,start,end; QueryPerformanceFrequency(&freq);
    double switchMs[5] = {0}, moveMs[5] = {0};
    // Only the test-owned window is moved. Existing desktops are never removed.
    for (int i=count; i<5; ++i) if (a.create()<0) failed++;
    for (int i=0;i<5;++i) {
        QueryPerformanceCounter(&start);
        if (a.move(h,i)<0 || a.windowDesktop(h)!=i) failed++;
        QueryPerformanceCounter(&end); moveMs[i]=(end.QuadPart-start.QuadPart)*1000.0/freq.QuadPart;
        int before = changes;
        int previous = a.current();
        QueryPerformanceCounter(&start);
        if (a.go(i)<0) failed++;
        ULONGLONG deadline=GetTickCount64()+3000;
        while ((a.current()!=i || (previous!=i && changes==before)) && GetTickCount64()<deadline) pump(10);
        QueryPerformanceCounter(&end); switchMs[i]=(end.QuadPart-start.QuadPart)*1000.0/freq.QuadPart;
        if (a.current()!=i || (previous!=i && changes==before)) {
            fprintf(stderr,"Switch check failed: target=%d actual=%d notificationsBefore=%d after=%d\n",i,a.current(),before,changes);
            failed++;
        }
        pump(300);
    }
    if(a.go(original)<0) failed++;
    pump(400); a.unsubscribe(h); DestroyWindow(h);
    printf(",\"failures\":%d,\"notifications\":%d,\"switchAndNotificationMs\":[",failed,changes);
    for(int i=0;i<5;++i) printf("%s%.3f",i?",":"",switchMs[i]);
    printf("],\"moveAndVerifyMs\":[");
    for(int i=0;i<5;++i) printf("%s%.3f",i?",":"",moveMs[i]);
    puts("]}"); CoUninitialize(); return failed?1:0;
}

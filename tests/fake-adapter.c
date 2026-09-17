// Fault injection runs in a separate test directory/process, never the live DLL.
#include <windows.h>
#undef CreateDesktop
#define API __declspec(dllexport)
#ifndef FAULT_MODE
#define FAULT_MODE 0
#endif
static int current;
API int GetHelperAdapterVersion(void) { return 2; }
API void ResetHelperDesktopService(void) { if(FAULT_MODE>=5)current=1; }
API int GetDesktopCount(void) { if(FAULT_MODE==2) Sleep(INFINITE); return 5; }
API int GetCurrentDesktopNumber(void) { return current; }
API int CreateDesktop(void) { return 4; }
API int GoToDesktopNumber(int n) { current=n; return 1; }
API int MoveWindowToDesktopNumber(HWND h,int n) { (void)h;(void)n; return 1; }
API int GetWindowDesktopNumber(HWND h) { (void)h; return 0; }
API int PinWindow(HWND h) { (void)h; return 1; }
API int IsPinnedWindow(HWND h) { (void)h; return 1; }
API int RegisterPostMessageHook(HWND h,UINT message) {
    (void)h;(void)message; static int attempts;
    ++attempts;
    return FAULT_MODE==1 || ((FAULT_MODE==4 || FAULT_MODE==6) && attempts<3) ? -1:1;
}
API void UnregisterPostMessageHook(HWND h) { (void)h; if(FAULT_MODE==3) Sleep(INFINITE); }

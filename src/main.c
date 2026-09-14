#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include "service.h"
#include "input.h"
#include "diagnostics.h"
#include "native_taskbar.h"

#define TRAY_MESSAGE (WM_APP + 2)
#define DEADLINE_TIMER 1
#define RETRY_TIMER 2
#define ID_EXIT 20
#define ID_RETRY 22
static HWND host;
static UINT taskbarCreated;
static NOTIFYICONDATAW tray;
static unsigned retries;
static BOOL wasReady;
static ServiceError lastError;
// Stable identity shared with the native component and diagnostic tools.
static const wchar_t *className = L"DesktopsHelper.Widget";

static void updateTray(void) {
    const wchar_t *status = !service_state().ready ? L"Desktop service unavailable" :
        native_taskbar_active() ? L"Taskbar buttons ready" :
        native_taskbar_pending() ? L"Connecting taskbar buttons" : L"Taskbar unavailable; use Reconnect";
    wchar_t tip[ARRAYSIZE(tray.szTip)];
    swprintf(tip, ARRAYSIZE(tip), L"Desktops Helper | %ls", status);
    if(!wcscmp(tray.szTip,tip))return;
    wcscpy(tray.szTip,tip);
    tray.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &tray);
}
static void notify(const wchar_t *message) {
    wcsncpy(tray.szInfo, message, ARRAYSIZE(tray.szInfo) - 1);
    wcscpy(tray.szInfoTitle, L"Desktops Helper");
    tray.dwInfoFlags = NIIF_WARNING;
    tray.uFlags = NIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &tray);
}

static void addTray(void) {
    tray.cbSize = sizeof(tray); tray.hWnd = host; tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = TRAY_MESSAGE;
    tray.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcscpy(tray.szTip, L"Desktops Helper | Win+1..5 | Shift+Win+1..5 | Right-click to exit");
    Shell_NotifyIconW(NIM_ADD, &tray);
}

static void updateService(void) {
    ServiceState state=service_state();
    int current=state.ready ? state.current : -1;
    SetPropW(host,L"DesktopsHelper.Current",(HANDLE)(INT_PTR)(current+1));
    SetPropW(host,L"DesktopsHelper.Connection",(HANDLE)(UINT_PTR)state.generation);
    if(state.ready) {
        SetPropW(host,L"DesktopsHelper.Ready",(HANDLE)1);
        retries=0; KillTimer(host,RETRY_TIMER);
    } else RemovePropW(host,L"DesktopsHelper.Ready");
    if(state.pending && !state.timedOut) {
        ULONGLONG elapsed=GetTickCount64()-state.started;
        SetTimer(host,DEADLINE_TIMER,elapsed>=5000 ? 1 : (UINT)(5000-elapsed),NULL);
    } else KillTimer(host,DEADLINE_TIMER);
    if(state.error==SERVICE_CONNECT_ERROR && !state.pending && !state.busy && retries<5) {
        SetTimer(host,RETRY_TIMER,250U << retries,NULL); ++retries;
    }
    if(state.error!=lastError) {
        if(state.error==SERVICE_TIMEOUT) notify(L"Windows desktop service stopped responding. Shortcuts are released. Use Reconnect or restart the helper.");
        if(state.error==SERVICE_MOVE_ERROR) notify(L"This window could not be moved. It may be pinned, elevated, or no longer available.");
    }
    if(state.error==SERVICE_CONNECT_ERROR && retries==5 && !state.pending)
        notify(L"Could not connect to Windows desktops. Shortcuts are released. Use Reconnect after Explorer is available.");
    if(state.ready && !wasReady)native_taskbar_begin();
    wasReady=state.ready; lastError=state.error; native_taskbar_publish();updateTray();
}

static void menu(void) {
    HMENU m = CreatePopupMenu(); POINT p; GetCursorPos(&p);
    AppendMenuW(m, MF_STRING, ID_RETRY, L"Reconnect taskbar and desktop service");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Exit Desktops Helper");
    SetForegroundWindow(host);
    int choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, host, NULL);
    DestroyMenu(m); PostMessageW(host, WM_NULL, 0, 0);
    if (choice == ID_EXIT) DestroyWindow(host);
    if (choice == ID_RETRY) { native_taskbar_detach();updateTray();retries=0;service_connect();updateService(); }
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if(native_taskbar_handle(msg,wp,lp)){updateTray();return 0;}
    if (taskbarCreated && msg == taskbarCreated) {
        // Drop stale desktop COM subscriptions after a real Explorer replacement.
        // The new helper waits for this process to exit before taking its mutex.
        if(native_taskbar_restart_for_shell()){DestroyWindow(hwnd);return 0;}
        native_taskbar_detach();
        addTray();
        retries=0; service_connect(); updateService();
        return 0;
    }
    switch (msg) {
    case WM_TIMER:
        if(wp==NATIVE_ATTACH_TIMER){native_taskbar_tick();updateTray();}
        if(wp==DEADLINE_TIMER) {
            KillTimer(hwnd,DEADLINE_TIMER);
            ServiceState state=service_state();
            if(state.pending && !state.timedOut && GetTickCount64()-state.started>=5000) service_timeout();
            else updateService();
        }
        if(wp==RETRY_TIMER) {
            KillTimer(hwnd,RETRY_TIMER);
            ServiceState state=service_state();
            if(!state.ready && !state.pending && !state.timedOut) service_connect();
            updateService();
        }
        return 0;
    case SERVICE_UPDATE: updateService(); return 0;
    case TRAY_MESSAGE: if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) menu(); return 0;
    case WM_DESTROY:
        KillTimer(hwnd,DEADLINE_TIMER); KillTimer(hwnd,RETRY_TIMER);
        native_taskbar_stop();
        RemovePropW(hwnd,L"DesktopsHelper.Ready"); RemovePropW(hwnd,L"DesktopsHelper.Current");
        RemovePropW(hwnd,L"DesktopsHelper.Connection");
        if(!input_stop(2000) || !service_stop(2000)) {
            // A hung worker could own loader/COM locks. Skip DLL detach entirely.
            TerminateProcess(GetCurrentProcess(),2);
        }
        Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, PWSTR command, int show) {
    (void)prev; (void)show;
    // Legacy --native-taskbar shortcuts still work; native is now the only UI.
    const wchar_t* waiting=wcsstr(command,L"--wait-for-pid=");
    if(waiting){
        DWORD pid=wcstoul(waiting+wcslen(L"--wait-for-pid="),NULL,10);
        if(!pid || pid==GetCurrentProcessId())return 1;
        HANDLE previous=OpenProcess(SYNCHRONIZE,FALSE,pid);
        if(previous){DWORD result=WaitForSingleObject(previous,10000);CloseHandle(previous);if(result!=WAIT_OBJECT_0)return 1;}
    }
    diagnostics_start();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\DesktopsHelper.Singleton");
    if (!mutex) return 1;
    // Retained handles can keep a mutex alive after its process exits.
    // Own it for the UI thread's lifetime instead of testing existence.
    DWORD lockResult = WaitForSingleObject(mutex, 0);
    if (lockResult != WAIT_OBJECT_0 && lockResult != WAIT_ABANDONED) {
        CloseHandle(mutex); return lockResult == WAIT_TIMEOUT ? 0 : 1;
    }
    // Older builds created the mutex without owning it.
    if (FindWindowW(className, NULL)) { ReleaseMutex(mutex); CloseHandle(mutex); return 0; }
    WNDCLASSW wc = {.lpfnWndProc = windowProc, .hInstance = instance, .lpszClassName = className};
    RegisterClassW(&wc); taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    host = CreateWindowExW(WS_EX_TOOLWINDOW, className, L"Desktops Helper", WS_POPUP,
        0,0,0,0,NULL,NULL,instance,NULL);
    if (!host) { ReleaseMutex(mutex); CloseHandle(mutex); return 1; }
    native_taskbar_init(host);
    // Hidden top-level host receives Explorer broadcasts; never shown or pinned.
    addTray();
    if(!input_start() || !service_start(host)) {
        MessageBoxW(NULL,L"Could not start desktop helper threads.",L"Desktops Helper",MB_ICONERROR);
        DestroyWindow(host); return 1;
    }
    updateService();
    MSG msg; while (GetMessageW(&msg,NULL,0,0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    // The adapter owns worker threads; process exit unloads it after unsubscription.
    ReleaseMutex(mutex); CloseHandle(mutex); return 0;
}

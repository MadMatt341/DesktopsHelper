#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <shellapi.h>
#include "service.h"
#include "input.h"
#include "diagnostics.h"

#define TRAY_MESSAGE (WM_APP + 2)
#define CHECK_VISIBILITY (WM_APP + 4)
#define DEADLINE_TIMER 1
#define RETRY_TIMER 2
#define ID_EXIT 20
#define ID_POSITION 21
#define ID_RETRY 22
static HWND widget;
static HFONT regularFont, boldFont;
static HBRUSH background;
static int current = -1, cell = 32, height = 32;
static BOOL aboveTaskbar;
static UINT taskbarCreated;
static NOTIFYICONDATAW tray;
static ITaskbarList *taskbar;
static unsigned retries;
static BOOL wasReady;
static ServiceError lastError;
static HWINEVENTHOOK foregroundEvents, reorderEvents;
static LONG visibilityPending;
static const wchar_t *className = L"DesktopsHelper.Widget";

static void ensureVisible(void) {
    RECT r;
    if (!GetWindowRect(widget, &r) || !IsWindowVisible(widget)) return;
    POINT center = {(r.left + r.right)/2, (r.top + r.bottom)/2};
    HWND covering = GetAncestor(WindowFromPoint(center), GA_ROOT);
    if (covering && covering == FindWindowW(L"Shell_TrayWnd", NULL))
        SetWindowPos(widget, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void CALLBACK stackingChanged(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
    LONG object, LONG child, DWORD thread, DWORD time) {
    (void)hook; (void)object; (void)child; (void)thread; (void)time;
    if (event == EVENT_OBJECT_REORDER && hwnd && hwnd != GetDesktopWindow() &&
        GetAncestor(hwnd,GA_ROOT) != FindWindowW(L"Shell_TrayWnd",NULL)) return;
    // Coalesce Explorer/window stacking events. Never poll or steal focus.
    if (InterlockedCompareExchange(&visibilityPending, 1, 0) == 0 &&
        !PostMessageW(widget, CHECK_VISIBILITY, 0, 0)) InterlockedExchange(&visibilityPending,0);
}

static void notify(const wchar_t *message) {
    wcsncpy(tray.szInfo, message, ARRAYSIZE(tray.szInfo) - 1);
    wcscpy(tray.szInfoTitle, L"Desktops Helper");
    tray.dwInfoFlags = NIIF_WARNING;
    tray.uFlags = NIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &tray);
}

static void addTray(void) {
    tray.cbSize = sizeof(tray); tray.hWnd = widget; tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = TRAY_MESSAGE;
    tray.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcscpy(tray.szTip, L"Desktops Helper | Win+1..5 | Shift+Win+1..5 | Right-click to exit");
    Shell_NotifyIconW(NIM_ADD, &tray);
}

static void position(void) {
    MONITORINFO mi = {.cbSize = sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint((POINT){0,0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    UINT dpi = GetDpiForWindow(widget);
    cell = MulDiv(32, dpi, 96); height = MulDiv(32, dpi, 96);
    int margin = MulDiv(8, dpi, 96);
    APPBARDATA bar = {.cbSize = sizeof(bar)};
    int x = mi.rcMonitor.left + margin, y = mi.rcWork.bottom - height - margin;
    if (SHAppBarMessage(ABM_GETTASKBARPOS, &bar) && bar.uEdge == ABE_BOTTOM && !aboveTaskbar &&
        !(SHAppBarMessage(ABM_GETSTATE, &bar) & ABS_AUTOHIDE)) {
        y = bar.rc.top + ((bar.rc.bottom - bar.rc.top) - height) / 2;
    }
    SetWindowPos(widget, HWND_TOPMOST, x, y, cell * 5, height, SWP_NOACTIVATE);
    HFONT oldRegular = regularFont, oldBold = boldFont;
    regularFont = CreateFontW(-MulDiv(15, dpi, 96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    boldFont = CreateFontW(-MulDiv(15, dpi, 96),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if (oldRegular) DeleteObject(oldRegular);
    if (oldBold) DeleteObject(oldBold);
    InvalidateRect(widget, NULL, FALSE);
}

static void updateService(void) {
    ServiceState state=service_state();
    int next=state.ready ? state.current : -1;
    if(next!=current) { current=next; InvalidateRect(widget,NULL,FALSE); }
    SetPropW(widget,L"DesktopsHelper.Current",(HANDLE)(INT_PTR)(current+1));
    SetPropW(widget,L"DesktopsHelper.Connection",(HANDLE)(UINT_PTR)state.generation);
    if(state.ready) {
        SetPropW(widget,L"DesktopsHelper.Ready",(HANDLE)1);
        retries=0; KillTimer(widget,RETRY_TIMER);
        if(!wasReady && taskbar) ITaskbarList_DeleteTab(taskbar,widget);
    } else RemovePropW(widget,L"DesktopsHelper.Ready");
    if(state.pending && !state.timedOut) {
        ULONGLONG elapsed=GetTickCount64()-state.started;
        SetTimer(widget,DEADLINE_TIMER,elapsed>=5000 ? 1 : (UINT)(5000-elapsed),NULL);
    } else KillTimer(widget,DEADLINE_TIMER);
    if(state.error==SERVICE_CONNECT_ERROR && !state.pending && !state.busy && retries<5) {
        SetTimer(widget,RETRY_TIMER,250U << retries,NULL); ++retries;
    }
    if(state.error!=lastError) {
        if(state.error==SERVICE_TIMEOUT) notify(L"Windows desktop service stopped responding. Shortcuts are released. Use Reconnect or restart the helper.");
        if(state.error==SERVICE_MOVE_ERROR) notify(L"This window could not be moved. It may be pinned, elevated, or no longer available.");
    }
    if(state.error==SERVICE_CONNECT_ERROR && retries==5 && !state.pending)
        notify(L"Could not connect to Windows desktops. Shortcuts are released. Use Reconnect after Explorer is available.");
    wasReady=state.ready; lastError=state.error; ensureVisible();
}

static void menu(void) {
    HMENU m = CreatePopupMenu(); POINT p; GetCursorPos(&p);
    AppendMenuW(m, MF_STRING | (aboveTaskbar ? MF_CHECKED : 0), ID_POSITION, L"Place above taskbar");
    AppendMenuW(m, MF_STRING, ID_RETRY, L"Reconnect desktop service");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Exit Desktops Helper");
    SetForegroundWindow(widget);
    int choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, widget, NULL);
    DestroyMenu(m); PostMessageW(widget, WM_NULL, 0, 0);
    if (choice == ID_EXIT) DestroyWindow(widget);
    if (choice == ID_POSITION) { aboveTaskbar = !aboveTaskbar; position(); }
    if (choice == ID_RETRY) { retries=0; service_connect(); updateService(); }
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbarCreated && msg == taskbarCreated) {
        addTray(); position();
        if (taskbar) ITaskbarList_DeleteTab(taskbar, hwnd);
        retries=0; service_connect(); updateService();
        return 0;
    }
    switch (msg) {
    case WM_TIMER:
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
    case CHECK_VISIBILITY:
        InterlockedExchange(&visibilityPending, 0);
        ensureVisible(); return 0;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONUP: service_submit(GET_X_LPARAM(lp) / cell, FALSE, NULL); return 0;
    case WM_CONTEXTMENU: menu(); return 0;
    case TRAY_MESSAGE: if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) menu(); return 0;
    case WM_DPICHANGED: case WM_DISPLAYCHANGE: case WM_SETTINGCHANGE: position(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); RECT rect; GetClientRect(hwnd, &rect);
        FillRect(dc, &rect, background); SetBkMode(dc, TRANSPARENT);
        HGDIOBJ old = SelectObject(dc, regularFont);
        for (int i = 0; i < 5; ++i) {
            RECT r = {i * cell, 0, (i + 1) * cell, height}; wchar_t digit[] = {(wchar_t)(L'1' + i), 0};
            SelectObject(dc, i == current ? boldFont : regularFont);
            SetTextColor(dc, i == current ? RGB(255,255,255) : RGB(166,173,185));
            DrawTextW(dc, digit, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (i == current) {
                RECT line = {r.left + cell/3, height - 4, r.right - cell/3, height - 2};
                SetDCBrushColor(dc, RGB(132,182,255)); FillRect(dc, &line, GetStockObject(DC_BRUSH));
            }
        }
        SelectObject(dc, old); EndPaint(hwnd, &ps); return 0;
    }
    case WM_DESTROY:
        if (foregroundEvents) UnhookWinEvent(foregroundEvents);
        if (reorderEvents) UnhookWinEvent(reorderEvents);
        KillTimer(hwnd,DEADLINE_TIMER); KillTimer(hwnd,RETRY_TIMER);
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
    diagnostics_start();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\DesktopsHelper.Singleton");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) { if (mutex) CloseHandle(mutex); return 0; }
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { CloseHandle(mutex); return 1; }
    aboveTaskbar = wcsstr(command, L"--above-taskbar") != NULL;
    background = CreateSolidBrush(RGB(28,31,38));
    WNDCLASSW wc = {.lpfnWndProc = windowProc, .hInstance = instance, .hCursor = LoadCursorW(NULL, IDC_HAND), .lpszClassName = className};
    RegisterClassW(&wc); taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    widget = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, className, L"Desktops Helper", WS_POPUP,
        0,0,160,32,NULL,NULL,instance,NULL);
    if (!widget) return 1;
    position(); ShowWindow(widget, SW_SHOWNOACTIVATE);
    addTray();
    if (SUCCEEDED(CoCreateInstance(&CLSID_TaskbarList,NULL,CLSCTX_INPROC_SERVER,&IID_ITaskbarList,(void**)&taskbar)))
        ITaskbarList_HrInit(taskbar);
    if(!input_start() || !service_start(widget)) {
        MessageBoxW(NULL,L"Could not start desktop helper threads.",L"Desktops Helper",MB_ICONERROR);
        DestroyWindow(widget); return 1;
    }
    foregroundEvents = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        NULL, stackingChanged, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    reorderEvents = SetWinEventHook(EVENT_OBJECT_REORDER, EVENT_OBJECT_REORDER,
        NULL, stackingChanged, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if(!foregroundEvents || !reorderEvents) {
        MessageBoxW(NULL,L"Could not monitor taskbar visibility.",L"Desktops Helper",MB_ICONERROR);
        DestroyWindow(widget); return 1;
    }
    updateService();
    MSG msg; while (GetMessageW(&msg,NULL,0,0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    DeleteObject(regularFont); DeleteObject(boldFont); DeleteObject(background);
    if (taskbar) ITaskbarList_Release(taskbar);
    // The adapter owns worker threads; process exit unloads it after unsubscription.
    CoUninitialize(); CloseHandle(mutex); return 0;
}

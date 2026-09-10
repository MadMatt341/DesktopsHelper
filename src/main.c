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
static COLORREF textColor, mutedColor, selectedColor, hoverColor, accentColor;
static int hovered = -1;
static int current = -1, cell = 32, height = 32;
static BOOL aboveTaskbar;
static UINT taskbarCreated;
static NOTIFYICONDATAW tray;
static ITaskbarList *taskbar;
static unsigned retries;
static BOOL wasReady;
static ServiceError lastError;
static HWINEVENTHOOK foregroundEvents, reorderEvents, geometryEvents, lifecycleEvents;
static HWND lastForeground;
static DWORD observedThread;
static void CALLBACK stackingChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
static LONG visibilityPending;
static const wchar_t *className = L"DesktopsHelper.Widget";

static void ensureVisible(void) {
    HWND active = GetForegroundWindow();
    lastForeground = active;
    DWORD activeThread = active ? GetWindowThreadProcessId(active,NULL) : 0;
    if (foregroundEvents && activeThread && activeThread != observedThread) {
        HWINEVENTHOOK geometry = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
            NULL, stackingChanged, 0, activeThread, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        HWINEVENTHOOK lifecycle = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE,
            NULL, stackingChanged, 0, activeThread, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (geometry && lifecycle) {
            if (geometryEvents) UnhookWinEvent(geometryEvents);
            if (lifecycleEvents) UnhookWinEvent(lifecycleEvents);
            geometryEvents=geometry; lifecycleEvents=lifecycle; observedThread=activeThread;
        } else {
            if (geometry) UnhookWinEvent(geometry);
            if (lifecycle) UnhookWinEvent(lifecycle);
        }
    }
    BOOL fullscreen = FALSE;
    if (active && active != widget && IsWindowVisible(active) && !IsIconic(active)) {
        wchar_t cls[64] = {0};
        GetClassNameW(active, cls, ARRAYSIZE(cls));
        HMONITOR monitor = MonitorFromWindow(widget, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {.cbSize = sizeof(mi)};
        RECT client;
        if (wcscmp(cls,L"Shell_TrayWnd") && wcscmp(cls,L"Shell_SecondaryTrayWnd") &&
            wcscmp(cls,L"Progman") && wcscmp(cls,L"WorkerW") &&
            MonitorFromWindow(active,MONITOR_DEFAULTTONULL) == monitor &&
            GetMonitorInfoW(monitor,&mi) && GetClientRect(active,&client)) {
            POINT origin = {0,0};
            if (ClientToScreen(active,&origin))
                fullscreen = origin.x <= mi.rcMonitor.left && origin.y <= mi.rcMonitor.top &&
                    origin.x + client.right >= mi.rcMonitor.right &&
                    origin.y + client.bottom >= mi.rcMonitor.bottom;
        }
    }
    if (fullscreen) {
        if (IsWindowVisible(widget)) ShowWindow(widget,SW_HIDE);
        return;
    }
    if (!IsWindowVisible(widget)) {
        ShowWindow(widget,SW_SHOWNOACTIVATE);
        if (taskbar) ITaskbarList_DeleteTab(taskbar,widget);
    }
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
    if (event >= EVENT_OBJECT_DESTROY && event != EVENT_OBJECT_REORDER) {
        if (object != OBJID_WINDOW || child != CHILDID_SELF ||
            (event != EVENT_OBJECT_LOCATIONCHANGE && event != EVENT_OBJECT_HIDE &&
             event != EVENT_OBJECT_SHOW && event != EVENT_OBJECT_DESTROY) ||
            (hwnd != GetForegroundWindow() && hwnd != lastForeground)) return;
    }
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

static void appearance(void) {
    DWORD light=0, size=sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme",RRF_RT_REG_DWORD,NULL,&light,&size);
    HIGHCONTRASTW hc={.cbSize=sizeof(hc)};
    BOOL contrast=SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0) && (hc.dwFlags&HCF_HIGHCONTRASTON);
    COLORREF base=contrast ? GetSysColor(COLOR_WINDOW) : light ? RGB(243,243,243) : RGB(32,32,32);
    textColor=contrast ? GetSysColor(COLOR_WINDOWTEXT) : light ? RGB(24,24,24) : RGB(250,250,250);
    mutedColor=contrast ? textColor : light ? RGB(91,91,91) : RGB(180,180,180);
    selectedColor=contrast ? base : light ? RGB(225,225,225) : RGB(53,53,53);
    hoverColor=contrast ? base : light ? RGB(232,232,232) : RGB(45,45,45);
    accentColor=contrast ? textColor : light ? RGB(0,95,184) : RGB(96,205,255);
    HBRUSH next=CreateSolidBrush(base);
    if(next) { if(background) DeleteObject(background); background=next; }
}

static void position(void) {
    MONITORINFO mi = {.cbSize = sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint((POINT){0,0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    UINT dpi = GetDpiForWindow(widget);
    cell = MulDiv(30, dpi, 96); height = MulDiv(32, dpi, 96);
    int margin = MulDiv(8, dpi, 96);
    APPBARDATA bar = {.cbSize = sizeof(bar)};
    int x = mi.rcMonitor.left + margin, y = mi.rcWork.bottom - height - margin;
    if (SHAppBarMessage(ABM_GETTASKBARPOS, &bar) && bar.uEdge == ABE_BOTTOM && !aboveTaskbar &&
        !(SHAppBarMessage(ABM_GETSTATE, &bar) & ABS_AUTOHIDE)) {
        y = bar.rc.top + ((bar.rc.bottom - bar.rc.top) - height) / 2;
    }
    SetWindowPos(widget, HWND_TOPMOST, x, y, cell * 5, height, SWP_NOACTIVATE);
    int corner=MulDiv(8,dpi,96);
    HRGN region=CreateRoundRectRgn(0,0,cell*5+1,height+1,corner,corner);
    if(region && !SetWindowRgn(widget,region,TRUE)) DeleteObject(region);
    HFONT oldRegular = regularFont, oldBold = boldFont;
    regularFont = CreateFontW(-MulDiv(13, dpi, 96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH | FF_MODERN,L"Consolas");
    boldFont = CreateFontW(-MulDiv(13, dpi, 96),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH | FF_MODERN,L"Consolas");
    if (oldRegular) DeleteObject(oldRegular);
    if (oldBold) DeleteObject(oldBold);
    InvalidateRect(widget, NULL, FALSE);
    ensureVisible();
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
    case WM_MOUSEMOVE: {
        int next=GET_X_LPARAM(lp)/cell;
        if(next<0 || next>=5) next=-1;
        if(hovered<0 && next>=0) {
            TRACKMOUSEEVENT tracking={.cbSize=sizeof(tracking),.dwFlags=TME_LEAVE,.hwndTrack=hwnd};
            TrackMouseEvent(&tracking);
        }
        if(next!=hovered) { hovered=next; InvalidateRect(hwnd,NULL,FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE: hovered=-1; InvalidateRect(hwnd,NULL,FALSE); return 0;
    case WM_LBUTTONUP: service_submit(GET_X_LPARAM(lp) / cell, FALSE, NULL); return 0;
    case WM_CONTEXTMENU: menu(); return 0;
    case TRAY_MESSAGE: if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) menu(); return 0;
    case WM_THEMECHANGED: case WM_SYSCOLORCHANGE: case WM_SETTINGCHANGE:
        appearance(); position(); return 0;
    case WM_DPICHANGED: case WM_DISPLAYCHANGE: position(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); RECT rect; GetClientRect(hwnd, &rect);
        FillRect(dc, &rect, background); SetBkMode(dc, TRANSPARENT);
        HGDIOBJ old = SelectObject(dc, regularFont);
        HGDIOBJ oldBrush=SelectObject(dc,GetStockObject(DC_BRUSH));
        HGDIOBJ oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
        int inset=MulDiv(3,GetDpiForWindow(hwnd),96);
        for (int i = 0; i < 5; ++i) {
            RECT r = {i * cell, 0, (i + 1) * cell, height}; wchar_t digit[] = {(wchar_t)(L'1' + i), 0};
            if(i==current || i==hovered) {
                SetDCBrushColor(dc,i==current ? selectedColor : hoverColor);
                RoundRect(dc,r.left+inset,inset,r.right-inset,height-inset,inset*2,inset*2);
            }
            SelectObject(dc, i == current ? boldFont : regularFont);
            SetTextColor(dc, i == current || i==hovered ? textColor : mutedColor);
            DrawTextW(dc, digit, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (i == current) {
                SetDCBrushColor(dc,accentColor);
                RoundRect(dc,r.left+cell/3,height-inset*2,r.right-cell/3,height-inset,
                    inset,inset);
            }
        }
        SelectObject(dc,oldPen); SelectObject(dc,oldBrush);
        SelectObject(dc, old); EndPaint(hwnd, &ps); return 0;
    }
    case WM_DESTROY:
        if (foregroundEvents) UnhookWinEvent(foregroundEvents);
        if (reorderEvents) UnhookWinEvent(reorderEvents);
        if (geometryEvents) UnhookWinEvent(geometryEvents);
        if (lifecycleEvents) UnhookWinEvent(lifecycleEvents);
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
    appearance();
    WNDCLASSW wc = {.lpfnWndProc = windowProc, .hInstance = instance, .hCursor = LoadCursorW(NULL, IDC_HAND), .lpszClassName = className};
    RegisterClassW(&wc); taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    widget = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, className, L"Desktops Helper", WS_POPUP,
        0,0,160,32,NULL,NULL,instance,NULL);
    if (!widget) return 1;
    position();
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
    geometryEvents = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
        NULL, stackingChanged, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    lifecycleEvents = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE,
        NULL, stackingChanged, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if(!foregroundEvents || !reorderEvents || !geometryEvents || !lifecycleEvents) {
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

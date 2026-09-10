#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <objbase.h>
#include "service.h"
#include "adapter.h"
#include "diagnostics.h"

#define DESKTOP_EVENT (WM_APP + 1)
#define QUEUE_SIZE 16
typedef enum { CONNECT, ACTION, REFRESH } Kind;
typedef struct { Kind kind; int desktop; BOOL move; HWND target; DWORD pid; ULONGLONG submitted; } Command;
static SRWLOCK lock = SRWLOCK_INIT;
static ServiceState state = {.current = -1};
static Command queue[QUEUE_SIZE];
static unsigned head, count;
static HANDLE worker, wake;
static HWND host, receiver;
static BOOL stopping, refreshQueued;
static Adapter vd;
static BOOL subscribed;
static BOOL adapterAttempted, adapterLoaded;

static void changed(void) { PostMessageW(host, SERVICE_UPDATE, 0, 0); }
ServiceState service_state(void) {
    AcquireSRWLockShared(&lock); ServiceState result = state; ReleaseSRWLockShared(&lock); return result;
}
static BOOL enqueue(Command command) {
    // Called by the keyboard hook: never wait on another thread.
    if (command.kind==ACTION) {
        if (!TryAcquireSRWLockExclusive(&lock)) return FALSE;
    } else AcquireSRWLockExclusive(&lock);
    if (stopping || count == QUEUE_SIZE || (command.kind != CONNECT && !state.ready)) {
        ReleaseSRWLockExclusive(&lock); return FALSE;
    }
    if (command.kind == REFRESH && refreshQueued) { ReleaseSRWLockExclusive(&lock); return TRUE; }
    if (command.kind == REFRESH) refreshQueued = TRUE;
    if (!state.pending) state.started = GetTickCount64();
    queue[(head + count++) % QUEUE_SIZE] = command; state.pending = TRUE;
    ReleaseSRWLockExclusive(&lock);
    SetEvent(wake); changed(); return TRUE;
}
BOOL service_connect(void) {
    AcquireSRWLockExclusive(&lock);
    if (stopping || (state.busy && state.timedOut)) { ReleaseSRWLockExclusive(&lock); return FALSE; }
    if (state.busy) {
        // Explorer may restart while an operation is in flight. Do not lose that event.
        state.ready=FALSE; head=0;count=1;refreshQueued=FALSE;state.pending=TRUE;
        queue[0]=(Command){.kind=CONNECT};
        ReleaseSRWLockExclusive(&lock); SetEvent(wake); changed(); return TRUE;
    }
    state.ready = FALSE; state.timedOut = FALSE; state.error = SERVICE_OK;
    head = count = 0; refreshQueued = FALSE; state.pending = FALSE;
    ReleaseSRWLockExclusive(&lock);
    return enqueue((Command){.kind = CONNECT});
}
BOOL service_submit(int desktop, BOOL move, HWND target) {
    if (desktop < 0 || desktop >= 5) return FALSE;
    DWORD pid = 0;
    if (move && (!target || target == host || !GetWindowThreadProcessId(target, &pid))) return FALSE;
    return enqueue((Command){.kind=ACTION,.desktop=desktop,.move=move,.target=target,
        .pid=pid,.submitted=GetTickCount64()});
}
void service_timeout(void) {
    AcquireSRWLockExclusive(&lock);
    state.ready = FALSE; state.timedOut = TRUE; state.error = SERVICE_TIMEOUT;
    head = count = 0; refreshQueued = FALSE;
    ReleaseSRWLockExclusive(&lock); changed();
}
static LRESULT CALLBACK receiver_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == DESKTOP_EVENT) { enqueue((Command){.kind=REFRESH}); return 0; }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static ServiceError execute(Command c) {
    diagnostics_phase(10+(long)c.kind);
    if (c.kind == CONNECT) {
        if (!adapterAttempted) { adapterAttempted=TRUE; adapterLoaded=loadAdapter(&vd); }
        if (!adapterLoaded) return SERVICE_CONNECT_ERROR;
        if (subscribed) { vd.unsubscribe(receiver); subscribed = FALSE; }
        vd.reset();
        if (vd.count() < 1 || vd.pin(host) < 0 || vd.isPinned(host) != 1) return SERVICE_CONNECT_ERROR;
        if (vd.subscribe(receiver, DESKTOP_EVENT) < 0) return SERVICE_CONNECT_ERROR;
        subscribed = TRUE;
    } else if (c.kind == ACTION) {
        DWORD pid = 0;
        // A delayed move must never be redirected to a new foreground window.
        if (c.move && (GetTickCount64()-c.submitted > 2000 || !IsWindow(c.target) ||
            !GetWindowThreadProcessId(c.target,&pid) || pid != c.pid || c.target == GetShellWindow() ||
            c.target == FindWindowW(L"Shell_TrayWnd",NULL) || vd.isPinned(c.target) == 1)) return SERVICE_MOVE_ERROR;
        int desktops = vd.count();
        if (desktops < 1) return SERVICE_CONNECT_ERROR;
        for (int i=desktops; i<=c.desktop; ++i) if(vd.create()<0) return SERVICE_CONNECT_ERROR;
        if ((c.move ? vd.move(c.target,c.desktop) : vd.go(c.desktop)) < 0)
            return c.move ? SERVICE_MOVE_ERROR : SERVICE_CONNECT_ERROR;
        if (c.move && vd.windowDesktop(c.target) != c.desktop) return SERVICE_MOVE_ERROR;
    }
    int current = vd.current();
    if (current < 0) return SERVICE_CONNECT_ERROR;
    AcquireSRWLockExclusive(&lock); state.current = current; ReleaseSRWLockExclusive(&lock);
    return SERVICE_OK;
}
static DWORD WINAPI run(void *unused) {
    (void)unused;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        AcquireSRWLockExclusive(&lock); state.error=SERVICE_CONNECT_ERROR; state.pending=FALSE;
        ReleaseSRWLockExclusive(&lock); changed(); return 1;
    }
    WNDCLASSW wc={.lpfnWndProc=receiver_proc,.hInstance=GetModuleHandleW(NULL),.lpszClassName=L"DesktopsHelper.Service"};
    RegisterClassW(&wc);
    receiver=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,NULL,wc.hInstance,NULL);
    if (!receiver) { CoUninitialize(); return 1; }
    for (;;) {
        MsgWaitForMultipleObjects(1,&wake,FALSE,INFINITE,QS_ALLINPUT);
        MSG msg; while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        for (;;) {
            AcquireSRWLockExclusive(&lock);
            if (stopping) { ReleaseSRWLockExclusive(&lock); goto done; }
            if (!count) { ReleaseSRWLockExclusive(&lock); break; }
            Command command=queue[head]; head=(head+1)%QUEUE_SIZE; --count;
            if (command.kind==REFRESH) refreshQueued=FALSE;
            state.busy=TRUE; state.started=GetTickCount64();
            ReleaseSRWLockExclusive(&lock);
            changed();
            ServiceError error=execute(command);
            AcquireSRWLockExclusive(&lock);
            state.busy=FALSE; state.pending=count!=0;
            if (!state.timedOut) {
                state.error=error;
                if (error==SERVICE_CONNECT_ERROR) state.ready=FALSE;
                else if (command.kind==CONNECT && error==SERVICE_OK) { state.ready=TRUE; ++state.generation; }
            }
            if (count && queue[head].kind==CONNECT) state.ready=FALSE;
            else if (!state.ready) { head=count=0; refreshQueued=FALSE; state.pending=FALSE; }
            ReleaseSRWLockExclusive(&lock); changed();
        }
    }
done:
    diagnostics_phase(20);
    if (subscribed) vd.unsubscribe(receiver);
    if (vd.module && vd.reset) vd.reset();
    DestroyWindow(receiver); CoUninitialize(); return 0;
}
BOOL service_start(HWND widget) {
    host=widget; wake=CreateEventW(NULL,FALSE,FALSE,NULL);
    if (!wake) return FALSE;
    worker=CreateThread(NULL,0,run,NULL,0,NULL);
    if (!worker) { CloseHandle(wake); wake=NULL; return FALSE; }
    return service_connect();
}
BOOL service_stop(DWORD timeout) {
    AcquireSRWLockExclusive(&lock); stopping=TRUE; state.ready=FALSE; ReleaseSRWLockExclusive(&lock);
    if (!worker) return TRUE;
    SetEvent(wake);
    if (WaitForSingleObject(worker,timeout)!=WAIT_OBJECT_0) return FALSE;
    DWORD code=1; GetExitCodeThread(worker,&code);
    CloseHandle(worker); CloseHandle(wake); worker=wake=NULL; return code==0;
}

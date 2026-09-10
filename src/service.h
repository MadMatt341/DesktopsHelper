#ifndef SERVICE_H
#define SERVICE_H
#include <windows.h>
#define SERVICE_UPDATE (WM_APP + 5)
typedef enum { SERVICE_OK, SERVICE_CONNECT_ERROR, SERVICE_MOVE_ERROR, SERVICE_TIMEOUT } ServiceError;
typedef struct {
    BOOL ready, busy, pending, timedOut;
    int current;
    unsigned generation;
    ServiceError error;
    ULONGLONG started;
} ServiceState;
BOOL service_start(HWND widget);
BOOL service_connect(void);
BOOL service_submit(int desktop, BOOL move, HWND target);
ServiceState service_state(void);
void service_timeout(void);
BOOL service_stop(DWORD timeout);
#endif

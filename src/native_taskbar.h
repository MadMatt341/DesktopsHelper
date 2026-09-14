#ifndef NATIVE_TASKBAR_H
#define NATIVE_TASKBAR_H
#include <windows.h>
#define NATIVE_ATTACH_TIMER 3
void native_taskbar_init(HWND widget, BOOL enabled);
void native_taskbar_begin(void);
void native_taskbar_tick(void);
void native_taskbar_publish(void);
BOOL native_taskbar_active(void);
BOOL native_taskbar_handle(UINT message, WPARAM wp, LPARAM lp);
BOOL native_taskbar_restart_for_shell(void);
void native_taskbar_stop(void);
void native_taskbar_detach(void);
#endif

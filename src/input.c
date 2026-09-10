#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include "service.h"
#include "input.h"
static HANDLE thread, ready;
static DWORD threadId;
static HHOOK hook;
static BOOL swallowed[5];
static LRESULT CALLBACK keyboard(int code, WPARAM wp, LPARAM lp) {
    if (code==HC_ACTION) {
        KBDLLHOOKSTRUCT *key=(KBDLLHOOKSTRUCT*)lp;
        if (key->vkCode>='1' && key->vkCode<='5') {
            int i=(int)key->vkCode-'1'; BOOL up=wp==WM_KEYUP || wp==WM_SYSKEYUP;
            if (up && swallowed[i]) { swallowed[i]=FALSE; return 1; }
            if (!up && swallowed[i]) return 1;
            BOOL win=(GetAsyncKeyState(VK_LWIN)|GetAsyncKeyState(VK_RWIN))&0x8000;
            BOOL extra=(GetAsyncKeyState(VK_CONTROL)|GetAsyncKeyState(VK_MENU))&0x8000;
            if (!up && win && !extra) {
                HWND target=GetAncestor(GetForegroundWindow(),GA_ROOTOWNER);
                BOOL move=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;
                if (service_submit(i,move,target)) {
                    swallowed[i]=TRUE;
                    INPUT mask[2]={0}; mask[0].type=mask[1].type=INPUT_KEYBOARD;
                    mask[0].ki.wVk=mask[1].ki.wVk=0xE8; mask[1].ki.dwFlags=KEYEVENTF_KEYUP;
                    SendInput(2,mask,sizeof(INPUT)); return 1;
                }
            }
        }
    }
    return CallNextHookEx(hook,code,wp,lp);
}
static DWORD WINAPI run(void *unused) {
    (void)unused; MSG msg; PeekMessageW(&msg,NULL,0,0,PM_NOREMOVE);
    hook=SetWindowsHookExW(WH_KEYBOARD_LL,keyboard,GetModuleHandleW(NULL),0);
    SetEvent(ready);
    if (!hook) return 1;
    while(GetMessageW(&msg,NULL,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    UnhookWindowsHookEx(hook); return 0;
}
BOOL input_start(void) {
    ready=CreateEventW(NULL,TRUE,FALSE,NULL); if(!ready) return FALSE;
    thread=CreateThread(NULL,0,run,NULL,0,&threadId);
    if(!thread || WaitForSingleObject(ready,2000)!=WAIT_OBJECT_0 || !hook) return FALSE;
    CloseHandle(ready); ready=NULL; return TRUE;
}
BOOL input_stop(DWORD timeout) {
    if(!thread) return TRUE;
    PostThreadMessageW(threadId,WM_QUIT,0,0);
    if(WaitForSingleObject(thread,timeout)!=WAIT_OBJECT_0) return FALSE;
    CloseHandle(thread); thread=NULL;
    if(ready) { CloseHandle(ready); ready=NULL; }
    return TRUE;
}

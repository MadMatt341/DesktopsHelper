#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include "../src/service.h"
static BOOL commandAccepted=TRUE,win,rightWin,shift,ctrl,alt;
static int calls,lastDesktop; static BOOL lastMove;
BOOL service_submit(int n,BOOL move,HWND h) { (void)h; ++calls;lastDesktop=n;lastMove=move;return commandAccepted; }
static SHORT fakeState(int v) {return (v==VK_LWIN?win:v==VK_RWIN?rightWin:v==VK_SHIFT?shift:v==VK_CONTROL?ctrl:v==VK_MENU?alt:FALSE)?(SHORT)0x8000:0;}
static LRESULT fakeNext(HHOOK h,int c,WPARAM w,LPARAM l){(void)h;(void)c;(void)w;(void)l;return 7;}
static UINT fakeSend(UINT n,LPINPUT p,int cb){(void)p;(void)cb;return n;}
#define GetAsyncKeyState fakeState
#define CallNextHookEx fakeNext
#define SendInput fakeSend
#include "../src/input.c"
static LRESULT key(int n,BOOL up) {KBDLLHOOKSTRUCT k={.vkCode='1'+n};return keyboard(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,(LPARAM)&k);}
int main(void) {
    assert(key(0,FALSE)==7 && calls==0);
    win=TRUE;assert(key(0,FALSE)==1 && calls==1 && !lastMove && lastDesktop==0);
    assert(key(0,FALSE)==1 && calls==1); // repeat is swallowed, not repeated
    win=FALSE;assert(key(0,TRUE)==1); // release after Win is released
    win=shift=TRUE;assert(key(4,FALSE)==1 && lastMove && lastDesktop==4);assert(key(4,TRUE)==1);
    ctrl=TRUE;assert(key(2,FALSE)==7);ctrl=FALSE;alt=TRUE;assert(key(2,FALSE)==7);alt=FALSE;
    win=shift=FALSE;rightWin=TRUE;assert(key(3,FALSE)==1 && !lastMove);assert(key(3,TRUE)==1);
    commandAccepted=FALSE;assert(key(2,FALSE)==7 && key(2,TRUE)==7); // unavailable: pass through
    puts("Input policy: 9 cases passed");return 0;
}

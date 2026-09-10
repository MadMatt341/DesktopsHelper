#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include "../src/adapter.h"
static Adapter a;
static int first,second;
static LONG errors;
static DWORD WINAPI switching(void *unused) {
    (void)unused; CoInitializeEx(NULL,COINIT_MULTITHREADED);
    for(int i=0;i<40;++i) {
        if(a.go(i%2?first:second)<0) InterlockedIncrement(&errors);
        Sleep(10);
    }
    CoUninitialize();return 0;
}
int main(void) {
    CoInitializeEx(NULL,COINIT_MULTITHREADED);
    if(!loadAdapter(&a))return 1;
    int original=a.current(),count=a.count(); if(original<0 || count<1)return 1;
    int (__cdecl *removeDesktop)(int,int);
    FARPROC address=GetProcAddress(a.module,"RemoveDesktop");memcpy(&removeDesktop,&address,sizeof(address));
    if(!removeDesktop)return 1;
    first=a.create();second=a.create();if(first<0 || second<0)return 1;
    HWND h=CreateWindowW(L"STATIC",L"Adapter lifecycle test",0,0,0,1,1,HWND_MESSAGE,NULL,NULL,NULL);
    HANDLE thread=CreateThread(NULL,0,switching,NULL,0,NULL);
    for(int i=0;i<100;++i) {
        if(a.subscribe(h,WM_APP+1)<0) InterlockedIncrement(&errors);
        Sleep(1);a.unsubscribe(h);
    }
    if(WaitForSingleObject(thread,10000)!=WAIT_OBJECT_0)return 2;
    CloseHandle(thread);a.go(original);Sleep(300);
    if(removeDesktop(second,original)<0)++errors;
    if(removeDesktop(first,original)<0)++errors;
    if(a.count()!=count)++errors;
    DestroyWindow(h);CoUninitialize();
    printf("{\"subscriptionCycles\":100,\"concurrentSwitches\":40,\"failures\":%ld}\n",errors);
    return errors?1:0;
}

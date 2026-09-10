#include <windows.h>
#include <stdio.h>
#include "../src/adapter.h"
static int waitCurrent(Adapter *a,HWND h,int expected) {
    ULONGLONG until=GetTickCount64()+3000;
    while(GetTickCount64()<until) {
        MSG msg; while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
        if(a->current()==expected && (int)(INT_PTR)GetPropW(h,L"DesktopsHelper.Current")==expected+1) return 1;
        Sleep(10);
    }
    fprintf(stderr,"Topology timeout: expected=%d actual=%d indicator=%lld ready=%d\n",expected,a->current(),
        (long long)(INT_PTR)GetPropW(h,L"DesktopsHelper.Current"),GetPropW(h,L"DesktopsHelper.Ready")!=NULL);
    return 0;
}
int main(void) {
    Adapter a={0}; HWND h=FindWindowW(L"DesktopsHelper.Widget",NULL);
    if(!h || !GetPropW(h,L"DesktopsHelper.Ready") || !loadAdapter(&a)) return 1;
    int (__cdecl *reorder)(unsigned,unsigned);
    int (__cdecl *removeDesktop)(int,int);
    GUID (__cdecl *getId)(int);
    int (__cdecl *getIndex)(GUID);
#define LOAD_LOCAL(var,name) do { FARPROC p=GetProcAddress(a.module,name); memcpy(&var,&p,sizeof(p)); if(!var)return 1; } while(0)
    LOAD_LOCAL(reorder,"MoveHelperDesktop"); LOAD_LOCAL(removeDesktop,"RemoveDesktop");
    LOAD_LOCAL(getId,"GetDesktopIdByNumber"); LOAD_LOCAL(getIndex,"GetDesktopNumberById");
    int original=a.current(), n=a.count(), failed=0;
    if(original<0 || n<1 || a.create()!=n) return 1;
    GUID first=getId(n);
    if(a.create()!=n+1) {removeDesktop(getIndex(first),original);return 1;}
    GUID second=getId(n+1);
    HWND testWindow=CreateWindowW(L"STATIC",L"Desktop topology test",WS_OVERLAPPEDWINDOW,
        100,100,400,200,NULL,NULL,GetModuleHandleW(NULL),NULL);
    ShowWindow(testWindow,SW_SHOWNOACTIVATE);
    if(a.move(testWindow,n)<0)failed++;
    if(a.go(n)<0 || !waitCurrent(&a,h,n)) {fputs("Initial test desktop failed\n",stderr);failed++;}
    SetForegroundWindow(testWindow);
    BOOL focusEstablished=GetForegroundWindow()==testWindow;
    if(reorder((unsigned)n,(unsigned)n+1)<0 || !waitCurrent(&a,h,n+1)) {fputs("Reorder failed\n",stderr);failed++;}
    // Delete only the empty desktop we just created before the active test desktop.
    if(removeDesktop(getIndex(second),getIndex(first))<0 || !waitCurrent(&a,h,n)) {fputs("Delete preceding failed\n",stderr);failed++;}
    a.go(original); if(!waitCurrent(&a,h,original))failed++;
    DestroyWindow(testWindow);
    int index=getIndex(second); if(index>=0 && removeDesktop(index,original)<0)failed++;
    index=getIndex(first); if(index>=0 && removeDesktop(index,original)<0)failed++;
    if(a.count()!=n)failed++;
    printf("{\"topologyCases\":2,\"failures\":%d,\"desktopCountRestored\":%s,\"focusEstablished\":%s}\n",failed,a.count()==n?"true":"false",focusEstablished?"true":"false");
    return failed?1:0;
}

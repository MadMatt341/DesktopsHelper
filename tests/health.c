#include <windows.h>
#include <stdio.h>
int main(int argc,char **argv) {
    HWND h=FindWindowW(L"DesktopsHelper.Widget",NULL);
    if(!h) return 1;
    if(argc>1 && strcmp(argv[1],"reconnect")==0) {
        UINT_PTR before=(UINT_PTR)GetPropW(h,L"DesktopsHelper.Connection");
        PostMessageW(h,RegisterWindowMessageW(L"TaskbarCreated"),0,0);
        ULONGLONG deadline=GetTickCount64()+5000;
        while(GetTickCount64()<deadline) {
            if((UINT_PTR)GetPropW(h,L"DesktopsHelper.Connection")>before && GetPropW(h,L"DesktopsHelper.Ready") && !IsWindowVisible(h)) {
                puts("Reconnect event passed");return 0;
            }
            Sleep(10);
        }
        return 1;
    }
    DWORD_PTR ignored=0;
    BOOL responsive=SendMessageTimeoutW(h,WM_NULL,0,0,SMTO_ABORTIFHUNG,1000,&ignored)!=0;
    BOOL ready=GetPropW(h,L"DesktopsHelper.Ready")!=NULL;
    BOOL hidden=!IsWindowVisible(h);
    printf("{\"responsive\":%s,\"ready\":%s,\"hostHidden\":%s}\n",responsive?"true":"false",ready?"true":"false",hidden?"true":"false");
    return responsive && hidden && ready==(argc>1 && strcmp(argv[1],"ready")==0) ? 0:1;
}

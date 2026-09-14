#include <windows.h>
#include <cstdio>
#include "../src/taskbar-prototype/shared.h"

static LRESULT CALLBACK OwnerProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==RegisterWindowMessageW(DH_TASKBAR_REQUEST) && wp<=5) {
        PostMessageW(reinterpret_cast<HWND>(lp),RegisterWindowMessageW(DH_TASKBAR_STATE),
                     wp?wp:1,reinterpret_cast<LPARAM>(hwnd));
        return 0;
    }
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static HWND Endpoint(){return FindWindowExW(HWND_MESSAGE,nullptr,ControlClass,nullptr);}
static bool WaitAbsent() {
    for(unsigned i=0;i<200;i++){if(!Endpoint())return true;Sleep(10);}
    return false;
}
static PROCESS_INFORMATION Start(const wchar_t* file,const wchar_t* args) {
    wchar_t command[2048];
    swprintf_s(command,L"\"%ls\" %ls",file,args);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    PROCESS_INFORMATION child{};
    if(CreateProcessW(file,command,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))
        CloseHandle(child.hThread);
    return child;
}
static bool Run(const wchar_t* controller,const wchar_t* action,bool success) {
    auto child=Start(controller,action);
    if(!child.hProcess)return false;
    DWORD result=WaitForSingleObject(child.hProcess,6500),code=99;
    if(result==WAIT_OBJECT_0)GetExitCodeProcess(child.hProcess,&code);
    else TerminateProcess(child.hProcess,99);
    CloseHandle(child.hProcess);
    return result==WAIT_OBJECT_0 && (success?code==0:code!=0);
}
int wmain(int argc,wchar_t** argv) {
    if(argc>1 && !wcscmp(argv[1],L"--owner")) {
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpfnWndProc=OwnerProc;wc.lpszClassName=DH_HELPER_CLASS;
        if(!RegisterClassW(&wc))return 1;
        if(!CreateWindowW(DH_HELPER_CLASS,L"Lifecycle test owner",0,0,0,0,0,nullptr,nullptr,wc.hInstance,nullptr))return 1;
        MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
        return 0;
    }
    wchar_t self[MAX_PATH],controller[MAX_PATH];
    GetModuleFileNameW(nullptr,self,MAX_PATH);
    wcscpy_s(controller,self);
    auto slash=wcsrchr(controller,L'\\');if(!slash)return 1;
    wcscpy_s(slash+1,MAX_PATH-(slash+1-controller),L"TaskbarTestController.exe");
    if(FindWindowW(DH_HELPER_CLASS,nullptr) || Endpoint())return 1;
    auto owner=Start(self,L"--owner");
    if(!owner.hProcess)return 1;
    HWND window=nullptr;
    for(unsigned i=0;i<200 && !window;i++){window=FindWindowW(DH_HELPER_CLASS,nullptr);Sleep(10);}
    bool ok=window!=nullptr;
    for(unsigned stage=1;ok && stage<=12;stage++) {
        SetPropW(window,L"DesktopsHelper.TestFailStage",reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(stage)));
        ok=Run(controller,L"attach",false) && WaitAbsent() && GetPropW(window,DH_TASKBAR_ERROR);
        RemovePropW(window,L"DesktopsHelper.TestFailStage");
        // Successful remount verifies that original implicit columns were restored.
        ok=ok && Run(controller,L"attach",true) && Run(controller,L"status",true) &&
            Run(controller,L"remove",true) && WaitAbsent();
        printf("rollback stage %u and remount: %s\n",stage,ok?"PASS":"FAIL");fflush(stdout);
    }
    for(unsigned cycle=0;ok && cycle<100;cycle++) {
        ok=Run(controller,L"attach",true) && Run(controller,L"remove",true) && WaitAbsent();
        if(cycle%10==9 || !ok){printf("attach/remove cycle %u: %s\n",cycle+1,ok?"PASS":"FAIL");fflush(stdout);}
    }
    ok=ok && Run(controller,L"attach",true);
    // Kill only the disposable owner created above; the user's helper is untouched.
    TerminateProcess(owner.hProcess,17);
    WaitForSingleObject(owner.hProcess,3000);CloseHandle(owner.hProcess);
    ok=ok && WaitAbsent();
    printf("abrupt owner exit removes strip: %s\n",ok?"PASS":"FAIL");
    if(!ok)Run(controller,L"remove",true);
    return ok?0:1;
}

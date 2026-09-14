#include <windows.h>
#include <uiautomation.h>
#include <cstdio>
#include "../src/taskbar_protocol.h"
static int Current(HWND window,const wchar_t* property){return static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(window,property)));}
static bool WaitCurrent(HWND helper,HWND native,int desktop){
    for(int i=0;i<60;i++){if(Current(helper,L"DesktopsHelper.Current")==desktop && Current(native,L"DesktopsHelper.NativeCurrent")==desktop)return true;Sleep(100);}return false;
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 1;
    HWND helper=FindWindowW(DH_HELPER_CLASS,nullptr),native=FindWindowExW(HWND_MESSAGE,nullptr,DH_TASKBAR_CLASS,nullptr),taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);
    int original=Current(helper,L"DesktopsHelper.Current");
    if(!helper || !native || original<1 || original>5){puts("Native helper not ready on one of desktops 1..5");CoUninitialize();return 1;}
    IUIAutomation* automation=nullptr;IUIAutomationElement* root=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation));
    if(SUCCEEDED(hr))hr=automation->ElementFromHandle(taskbar,&root);
    if(FAILED(hr)){if(automation)automation->Release();CoUninitialize();return 1;}
    HWND foreground=GetForegroundWindow();POINT cursor{};GetCursorPos(&cursor);int failures=0;
    for(int desktop=1;desktop<=5;desktop++){
        wchar_t name[64];swprintf_s(name,L"Switch to desktop %d",desktop);
        VARIANT value{};value.vt=VT_BSTR;value.bstrVal=SysAllocString(name);
        IUIAutomationCondition* condition=nullptr;IUIAutomationElement* element=nullptr;
        hr=automation->CreatePropertyCondition(UIA_NamePropertyId,value,&condition);VariantClear(&value);
        if(SUCCEEDED(hr))hr=root->FindFirst(TreeScope_Descendants,condition,&element);
        if(condition)condition->Release();
        RECT rect{};if(SUCCEEDED(hr) && element)hr=element->get_CurrentBoundingRectangle(&rect);else hr=E_FAIL;
        if(element)element->Release();
        POINT center{(rect.left+rect.right)/2,(rect.top+rect.bottom)/2};
        bool hit=SUCCEEDED(hr) && rect.right>rect.left && GetAncestor(WindowFromPoint(center),GA_ROOT)==taskbar;
        bool ok=false;
        if(hit && SetCursorPos(center.x,center.y)){
            INPUT input[2]{};input[0].type=INPUT_MOUSE;input[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
            input[1].type=INPUT_MOUSE;input[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
            ok=SendInput(2,input,sizeof(INPUT))==2 && WaitCurrent(helper,native,desktop);
        }
        printf("native click %d: %s (hitTaskbar=%d)\n",desktop,ok?"PASS":"FAIL",hit);fflush(stdout);
        if(!ok){failures++;break;}
    }
    UINT request=RegisterWindowMessageW(DH_TASKBAR_REQUEST);
    int before=Current(helper,L"DesktopsHelper.Current");
    PostMessageW(helper,request,7,reinterpret_cast<LPARAM>(native));
    PostMessageW(helper,request,before==1?2:1,0);Sleep(250);
    bool invalidIgnored=Current(helper,L"DesktopsHelper.Current")==before;
    printf("invalid IPC requests ignored: %s\n",invalidIgnored?"PASS":"FAIL");failures+=!invalidIgnored;
    PostMessageW(helper,request,original,reinterpret_cast<LPARAM>(native));
    bool restored=WaitCurrent(helper,native,original);failures+=!restored;
    printf("starting desktop restored: %s\n",restored?"PASS":"FAIL");
    bool hidden=!IsWindowVisible(helper);printf("message host stays hidden: %s\n",hidden?"PASS":"FAIL");failures+=!hidden;
    SetCursorPos(cursor.x,cursor.y);if(foreground && IsWindow(foreground))SetForegroundWindow(foreground);
    root->Release();automation->Release();CoUninitialize();return failures?1:0;
}

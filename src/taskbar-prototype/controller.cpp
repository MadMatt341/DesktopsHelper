#include <windows.h>
#include <shlwapi.h>
#include <cstdio>
#include <vector>
#include "shared.h"
static bool CaptureTaskbar(HWND taskbar,const wchar_t* path) {
    RECT rect{};if(!GetWindowRect(taskbar,&rect))return false;
    int width=rect.right-rect.left,height=rect.bottom-rect.top;
    if(width<=0 || height<=0 || width>16000 || height>4000)return false;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!bitmap){DeleteDC(memory);ReleaseDC(nullptr,screen);return false;}
    HGDIOBJ old=SelectObject(memory,bitmap);
    BOOL copied=BitBlt(memory,0,0,width,height,screen,rect.left,rect.top,SRCCOPY|CAPTUREBLT);GdiFlush();
    FILE* file=nullptr;_wfopen_s(&file,path,L"wb");
    bool saved=false;
    if(file && copied){
        BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);
        DWORD bytes=static_cast<DWORD>(width*height*4);header.bfSize=header.bfOffBits+bytes;
        saved=fwrite(&header,sizeof(header),1,file)==1 && fwrite(&info.bmiHeader,sizeof(info.bmiHeader),1,file)==1 && fwrite(pixels,bytes,1,file)==1;
    }
    if(file)fclose(file);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);return saved;
}
static int WatchMenu() {
    wprintf(L"Recording for up to 60 seconds. Open Start or Search.\n");fflush(stdout);
    for(int sample=0;sample<600;sample++) {
        DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        wchar_t path[MAX_PATH]{};DWORD size=MAX_PATH;
        if(process){QueryFullProcessImageNameW(process,0,path,&size);CloseHandle(process);}
        const wchar_t* name=PathFindFileNameW(path);
        if(!_wcsicmp(name,L"SearchHost.exe") || !_wcsicmp(name,L"StartMenuExperienceHost.exe")) {
            Sleep(750);
            HWND taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);
            HWND control=FindWindowExW(HWND_MESSAGE,nullptr,ControlClass,nullptr);
            DWORD_PTR result=0;
            BOOL response=control && SendMessageTimeoutW(control,InspectButton,0,0,SMTO_ABORTIFHUNG,3000,&result);
            DWORD band=0;using Band=BOOL(WINAPI*)(HWND,PDWORD);
            auto getBand=reinterpret_cast<Band>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetWindowBand"));
            BOOL measured=getBand && getBand(taskbar,&band);
            wchar_t output[MAX_PATH];GetModuleFileNameW(nullptr,output,MAX_PATH);PathRemoveFileSpecW(output);PathAppendW(output,L"menu-taskbar.bmp");
            bool captured=CaptureTaskbar(taskbar,output);
            wprintf(L"Menu=%ls taskbarBand=%lu measured=%d buttonLayoutReady=%d capture=%d\n",name,band,measured,response && result!=0,captured);
            return response && result && captured?0:1;
        }
        Sleep(100);
    }
    wprintf(L"No Start/Search opening captured.\n");return 1;
}
int wmain(int argc,wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(argc>1 && !wcscmp(argv[1],L"watch-menu"))return WatchMenu();
    HWND control=FindWindowExW(HWND_MESSAGE,nullptr,ControlClass,nullptr);
    if(argc>1 && (!wcscmp(argv[1],L"remove") || !wcscmp(argv[1],L"status"))) {
        if(!control) {wprintf(L"Prototype is not attached.\n");return 1;}
        DWORD_PTR result=0;
        UINT message=!wcscmp(argv[1],L"remove") ? RemoveButton : InspectButton;
        if(!SendMessageTimeoutW(control,message,0,0,SMTO_ABORTIFHUNG,3000,&result)) return 2;
        wprintf(L"Native %ls result=%llu.\n",argv[1],static_cast<unsigned long long>(result));
        return result ? 0:1;
    }
    if(control) {
        DWORD_PTR ready=0;
        if(SendMessageTimeoutW(control,InspectButton,0,0,SMTO_ABORTIFHUNG,250,&ready) && ready)return 0;
        wprintf(L"Existing endpoint is not ready.\n");return 3;
    }
    HWND taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);DWORD pid=0;
    if(!taskbar || !GetWindowThreadProcessId(taskbar,&pid)) return 2;
    wchar_t dll[MAX_PATH];GetModuleFileNameW(nullptr,dll,MAX_PATH);PathRemoveFileSpecW(dll);
    if(!PathAppendW(dll,ComponentFile)) return 2;
    if(argc==1 || !wcscmp(argv[1],L"attach")) {
        HWND helper=FindWindowW(DH_HELPER_CLASS,nullptr);
        if(!helper)return 2;
        RemovePropW(helper,DH_TASKBAR_ERROR);
        const ULONGLONG deadline=GetTickCount64()+AttachTimeoutMs;
        HMODULE component=LoadLibraryW(dll);if(!component)return 2;
        auto callback=reinterpret_cast<HOOKPROC>(GetProcAddress(component,"BootstrapHook"));
        HHOOK hook=callback?SetWindowsHookExW(WH_CALLWNDPROC,callback,component,GetWindowThreadProcessId(taskbar,nullptr)):nullptr;
        if(!hook){wprintf(L"Bootstrap hook failed: %lu\n",GetLastError());FreeLibrary(component);return 2;}
        DWORD_PTR result=0;
        BOOL sent=SendMessageTimeoutW(taskbar,RegisterWindowMessageW(AttachMessage),
            reinterpret_cast<WPARAM>(helper),GetCurrentProcessId(),SMTO_ABORTIFHUNG,1000,&result)!=0;
        UnhookWindowsHookEx(hook);FreeLibrary(component);
        if(!sent)return 2;
        while(GetTickCount64()<deadline) {
            HWND mounted=FindWindowExW(HWND_MESSAGE,nullptr,ControlClass,nullptr);
            DWORD_PTR ready=0;
            DWORD remaining=static_cast<DWORD>(deadline-GetTickCount64());
            if(remaining>AttachTimeoutMs)break;
            DWORD timeout=remaining<100?remaining:100;
            if(mounted && SendMessageTimeoutW(mounted,InspectButton,0,0,SMTO_ABORTIFHUNG,timeout,&ready) && ready){wprintf(L"Native taskbar buttons attached.\n");return 0;}
            auto error=static_cast<DWORD>(reinterpret_cast<UINT_PTR>(GetPropW(helper,DH_TASKBAR_ERROR)));
            if(error){wprintf(L"Native attachment failed: 0x%08lX\n",error);return 3;}
            if(!IsWindow(helper))break;
            Sleep(25);
        }
        // Exiting cancels deferred work via our process handle. Remove a mount
        // that completed at the deadline before the inspection acknowledged it.
        HWND pending=FindWindowExW(HWND_MESSAGE,nullptr,ControlClass,nullptr);
        if(pending)PostMessageW(pending,RemoveButton,0,0);
        wprintf(L"Native attachment deadline expired.\n");return 3;
    }
    wprintf(L"Usage: TaskbarPrototype [attach|status|remove|watch-menu]\n");return 2;
}

#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <initializer_list>
static BOOL CALLBACK Found(PSYMBOL_INFO info,ULONG,void*) {
    printf("0x%llx %s\n",info->Address-info->ModBase,info->Name);return TRUE;
}
int main() {
    HMODULE library=LoadLibraryExW(L"C:\\Program Files (x86)\\Windows Kits\\10\\Debuggers\\x64\\dbghelp.dll",nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!library)return 1;
    auto init=reinterpret_cast<decltype(&SymInitialize)>(GetProcAddress(library,"SymInitialize"));
    auto options=reinterpret_cast<decltype(&SymSetOptions)>(GetProcAddress(library,"SymSetOptions"));
    auto load=reinterpret_cast<decltype(&SymLoadModuleEx)>(GetProcAddress(library,"SymLoadModuleEx"));
    auto enumerate=reinterpret_cast<decltype(&SymEnumSymbols)>(GetProcAddress(library,"SymEnumSymbols"));
    auto cleanup=reinterpret_cast<decltype(&SymCleanup)>(GetProcAddress(library,"SymCleanup"));
    if(!init || !options || !load || !enumerate || !cleanup)return 1;
    options(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
    HANDLE process=GetCurrentProcess();
    if(!init(process,"srv*C:\\github\\DesktopsHelper\\build\\symbols*https://msdl.microsoft.com/download/symbols",FALSE))return 2;
    DWORD64 base=load(process,nullptr,"C:\\Windows\\System32\\Taskbar.dll",nullptr,0,0,nullptr,0);
    if(!base){printf("Symbol load error=%lu\n",GetLastError());return 3;}
    for(auto mask:{"*TaskbarHost*"})
        if(!enumerate(process,base,mask,Found,nullptr))printf("Enumeration error=%lu\n",GetLastError());
    options(SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
    enumerate(process,base,"??_7CTaskBand*",Found,nullptr);
    cleanup(process);return 0;
}

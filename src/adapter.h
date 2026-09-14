#ifndef ADAPTER_H
#define ADAPTER_H
#include <windows.h>
#include <stdio.h>
typedef struct {
    HMODULE module;
    int (__cdecl *version)(void);
    void (__cdecl *reset)(void);
    int (__cdecl *count)(void);
    int (__cdecl *current)(void);
    int (__cdecl *create)(void);
    int (__cdecl *go)(int);
    int (__cdecl *move)(HWND, int);
    int (__cdecl *windowDesktop)(HWND);
    int (__cdecl *isPinned)(HWND);
    int (__cdecl *subscribe)(HWND, UINT);
    void (__cdecl *unsubscribe)(HWND);
} Adapter;
static int loadAdapter(Adapter *a) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return 0;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash || slash - path + 28 >= MAX_PATH) return 0;
    wcscpy(slash + 1, L"VirtualDesktopAccessor.dll");
    a->module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!a->module) return 0;
#define LOAD(field, name) do { FARPROC p = GetProcAddress(a->module, name); memcpy(&a->field, &p, sizeof(p)); if (!a->field) return 0; } while (0)
    LOAD(version, "GetHelperAdapterVersion");
    if (a->version() != 2) return 0;
    LOAD(reset, "ResetHelperDesktopService");
    LOAD(count, "GetDesktopCount"); LOAD(current, "GetCurrentDesktopNumber");
    LOAD(create, "CreateDesktop"); LOAD(go, "GoToDesktopNumber");
    LOAD(move, "MoveWindowToDesktopNumber"); LOAD(windowDesktop, "GetWindowDesktopNumber");
    LOAD(isPinned, "IsPinnedWindow");
    LOAD(subscribe, "RegisterPostMessageHook"); LOAD(unsubscribe, "UnregisterPostMessageHook");
#undef LOAD
    return 1;
}
#endif

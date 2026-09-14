#pragma once
#include <windows.h>
#include "../taskbar_protocol.h"
// Private prototype identity. No registration or persistent shell changes.
inline constexpr wchar_t ControlClass[]=DH_TASKBAR_CLASS;
inline constexpr UINT RemoveButton=DH_TASKBAR_REMOVE;
inline constexpr UINT InspectButton=WM_APP+72;
inline constexpr UINT OwnerExited=WM_APP+73;
inline constexpr DWORD AttachTimeoutMs=5000;
// wParam: helper HWND; lParam: controller PID. Neither is a memory pointer.
#ifdef DH_TASKBAR_TESTING
inline constexpr wchar_t AttachMessage[]=L"DesktopsHelper.Taskbar.Attach.v4.Test";
inline constexpr wchar_t ComponentFile[]=L"DesktopsHelper.TaskbarV4.Tests.dll";
#else
inline constexpr wchar_t AttachMessage[]=L"DesktopsHelper.Taskbar.Attach.v4";
inline constexpr wchar_t ComponentFile[]=L"DesktopsHelper.TaskbarV4.dll";
#endif

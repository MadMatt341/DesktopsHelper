#ifndef TASKBAR_PROTOCOL_H
#define TASKBAR_PROTOCOL_H
// Same-user, pointer-free protocol. 0 subscribes, 1..5 switch, 6 detaches.
#ifdef DH_TASKBAR_TESTING
#define DH_TASKBAR_CLASS L"DesktopsHelper.Taskbar.Control.v3.Test"
#define DH_HELPER_CLASS L"DesktopsHelper.Widget.Test"
#else
#define DH_TASKBAR_CLASS L"DesktopsHelper.Taskbar.Control.v3"
#define DH_HELPER_CLASS L"DesktopsHelper.Widget"
#endif
#define DH_TASKBAR_REQUEST L"DesktopsHelper.Taskbar.Request.v1"
#define DH_TASKBAR_STATE L"DesktopsHelper.Taskbar.State.v1"
#define DH_TASKBAR_REMOVE (WM_APP+71)
#define DH_TASKBAR_SUBSCRIBE 0
#define DH_TASKBAR_DETACH 6
#define DH_TASKBAR_UNAVAILABLE 0
#define DH_TASKBAR_OTHER_DESKTOP 6
#define DH_TASKBAR_MOUNTED L"DesktopsHelper.NativeMounted"
#define DH_TASKBAR_CURRENT L"DesktopsHelper.NativeCurrent"
#define DH_TASKBAR_OWNER L"DesktopsHelper.NativeOwner"
#define DH_TASKBAR_ERROR L"DesktopsHelper.NativeError"
#define DH_TASKBAR_CANCELLED L"DesktopsHelper.NativeCancelled"
#endif

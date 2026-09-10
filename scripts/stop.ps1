$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class HelperStop {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
'@
$window = [HelperStop]::FindWindow('DesktopsHelper.Widget', 'Desktops Helper')
if ($window -ne [IntPtr]::Zero) {
    [void][HelperStop]::PostMessage($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    Get-Process DesktopsHelper -ErrorAction SilentlyContinue | ForEach-Object {
        if (!$_.WaitForExit(5000)) { throw 'Helper did not exit within five seconds' }
    }
}

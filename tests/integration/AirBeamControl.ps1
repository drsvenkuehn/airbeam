## AirBeamControl.ps1 — PowerShell helper to interact with a running AirBeam instance.
## Finds AirBeam's hidden message window ("AirBeamTrayWnd") and sends WM_COMMAND messages,
## simulating tray menu selections without touching the mouse.
##
## Usage (dot-source or call directly):
##   . .\AirBeamControl.ps1
##   $hwnd = Get-AirBeamHwnd
##   Send-AirBeamCommand $hwnd "IDM_VOLUME"
##   Send-AirBeamCommand $hwnd "IDM_SHOW_MENU"
##   Send-AirBeamCommand $hwnd "IDM_DEVICE" 0   # connect to first discovered speaker

#Requires -Version 5.1

# ── Win32 P/Invoke ────────────────────────────────────────────────────────────

$Win32 = @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Win32 {
    public delegate bool EnumWndProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWndProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);

    public static IntPtr FindByClassAndPid(uint targetPid, string className) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == targetPid) {
                var sb = new StringBuilder(256); GetClassName(h, sb, 256);
                if (sb.ToString() == className) { result = h; return false; }
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
}
"@

if (-not ([System.Management.Automation.PSTypeName]'Win32').Type) {
    Add-Type -TypeDefinition $Win32 -Language CSharp
}

# ── Command ID table (mirrors src/core/Commands.h) ───────────────────────────

$AirBeamCommands = @{
    IDM_QUIT                  = 1000
    IDM_CHECK_UPDATES         = 1001
    IDM_OPEN_LOG_FOLDER       = 1002
    IDM_LOW_LATENCY_TOGGLE    = 1003
    IDM_LAUNCH_STARTUP_TOGGLE = 1004
    IDM_VOLUME                = 1005
    IDM_SHOW_MENU             = 1006
    IDM_DEVICE_BASE           = 2000
}

$WM_COMMAND = 0x0111

# ── Functions ─────────────────────────────────────────────────────────────────

function Get-AirBeamHwnd {
    <#
    .SYNOPSIS
        Finds the AirBeamTrayWnd window by enumerating the AirBeam process's windows.
        Returns [IntPtr] or $null if AirBeam is not running.
        Note: FindWindowW fails when called cross-session; EnumWindows works reliably.
    #>
    $proc = Get-Process AirBeam -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) { return $null }
    $hwnd = [Win32]::FindByClassAndPid([uint32]$proc.Id, "AirBeamTrayWnd")
    if ($hwnd -ne [IntPtr]::Zero -and [Win32]::IsWindow($hwnd)) { return $hwnd }
    return $null
}

function Send-AirBeamCommand {
    <#
    .SYNOPSIS
        Sends a WM_COMMAND to the AirBeam window.
    .PARAMETER Hwnd
        HWND returned by Get-AirBeamHwnd.
    .PARAMETER CommandName
        One of the IDM_* keys from $AirBeamCommands, e.g. "IDM_VOLUME".
    .PARAMETER DeviceIndex
        For IDM_DEVICE: zero-based index of the discovered speaker (default 0).
    #>
    param(
        [Parameter(Mandatory)][IntPtr]$Hwnd,
        [Parameter(Mandatory)][string]$CommandName,
        [int]$DeviceIndex = 0
    )

    $id = if ($CommandName -eq "IDM_DEVICE") {
        $AirBeamCommands["IDM_DEVICE_BASE"] + $DeviceIndex
    } else {
        $AirBeamCommands[$CommandName]
    }

    if ($null -eq $id) {
        Write-Warning "Unknown command: $CommandName"
        return
    }

    $ok = [Win32]::PostMessage($Hwnd, $WM_COMMAND, [IntPtr]$id, [IntPtr]::Zero)
    if (-not $ok) {
        Write-Warning "PostMessage failed (error $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
    } else {
        Write-Host "  → Sent $CommandName (id=$id) to HWND $Hwnd" -ForegroundColor Cyan
    }
}

function Test-AirBeamRunning {
    $hwnd = Get-AirBeamHwnd
    return ($null -ne $hwnd)
}

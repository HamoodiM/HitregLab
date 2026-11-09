# Hitreg Lab: records the Shot Autopsy demo with ffmpeg (gdigrab of the first client's window).
# A dedicated server on Lvl_Peek runs a short behind-cover sweep at 150 ms round trip (cap 200, the default);
# the first client then opens the Shot Autopsy viewer on the newest hit (-AutopsyDemoAt) and auto-plays it.
#   autopsy.mp4   ~30 s of the viewer: the ray, the shooter's view, the server's rewound hitboxes, where the
#                 victim really was, and where the victim's own screen had it when the hit landed
# Only the processes this script starts are stopped.
param([string]$Out = "C:\UnrealEngineProjects\HitregLab\Saved\Video", [int]$OpenAt = 55, [int]$Seconds = 30)
$ue = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$p = '"C:\UnrealEngineProjects\HitregLab\HitregLab.uproject"'
$l = 'C:\UnrealEngineProjects\HitregLab\Saved\Logs'
New-Item -ItemType Directory -Force -Path $Out | Out-Null

Add-Type @"
using System; using System.Runtime.InteropServices;
public class CR3 {
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@
$procs = @()
try {
    $procs += Start-Process $ue -ArgumentList "$p /Game/HitregLab/Maps/Lvl_Peek -server -log -nosteam -CoverSweep=4 -CoverRtt=150 -CoverConfigs=base -abslog=$l\rec_autopsy_server.log" -WindowStyle Minimized -PassThru
    Start-Sleep 15
    $c = Start-Process $ue -ArgumentList "$p 127.0.0.1 -game -windowed -ResX=1280 -ResY=720 -WinX=40 -WinY=40 -nosound -nosteam -AutopsyDemoAt=$OpenAt -abslog=$l\rec_autopsy_c1.log" -PassThru
    $procs += $c
    Start-Sleep 5
    $procs += Start-Process $ue -ArgumentList "$p 127.0.0.1 -game -windowed -ResX=480 -ResY=270 -WinX=1340 -WinY=40 -nosound -nosteam -abslog=$l\rec_autopsy_c2.log" -PassThru
    Start-Sleep ($OpenAt - 5)
    $h = (Get-Process -Id $c.Id).MainWindowHandle
    [CR3]::SetForegroundWindow($h) | Out-Null
    Start-Sleep 2
    $pt = New-Object CR3+POINT; [CR3]::ClientToScreen($h, [ref]$pt) | Out-Null
    & ffmpeg -y -loglevel error -f gdigrab -framerate 30 -offset_x $pt.X -offset_y $pt.Y -video_size "1280x720" -t $Seconds -i desktop -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p (Join-Path $Out "autopsy.mp4")
    "recorded"
}
finally {
    foreach ($pr in $procs) { if ($pr -and -not $pr.HasExited) { Stop-Process -Id $pr.Id -Force -ErrorAction SilentlyContinue } }
}

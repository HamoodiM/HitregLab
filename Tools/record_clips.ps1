# Hitreg Lab: records the demo footage with ffmpeg (gdigrab of the game window's client area).
#   lagcomp_150.mp4  dedicated server on the range + a client running the hit-registration bench at 150 ms
#   killcam.mp4      standalone arena: the fight bots kill an idle player and the kill cam plays
param([string]$Out = "C:\UnrealEngineProjects\HitregLab\Saved\Video")
$ue = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$p = '"C:\UnrealEngineProjects\HitregLab\HitregLab.uproject"'
$l = 'C:\UnrealEngineProjects\HitregLab\Saved\Logs'
New-Item -ItemType Directory -Force -Path $Out | Out-Null

Add-Type @"
using System; using System.Runtime.InteropServices;
public class CR2 {
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@
function Record($proc, $file, $seconds) {
    $h = (Get-Process -Id $proc.Id).MainWindowHandle
    $pt = New-Object CR2+POINT; [CR2]::ClientToScreen($h, [ref]$pt) | Out-Null
    & ffmpeg -y -loglevel error -f gdigrab -framerate 30 -offset_x $pt.X -offset_y $pt.Y -video_size "1280x720" -t $seconds -i desktop -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p (Join-Path $Out $file)
}

# 1) Lag compensation at 150 ms
Start-Process $ue -ArgumentList "$p /Game/HitregLab/Maps/Lvl_Range -server -log -nosteam -abslog=$l\rec_server.log" -WindowStyle Minimized
Start-Sleep 15
$c = Start-Process $ue -ArgumentList "$p 127.0.0.1 -game -windowed -ResX=1280 -ResY=720 -WinX=40 -WinY=40 -nosound -nosteam -HitregBench=16 -BenchRtt=150 -QuitAfterBench -abslog=$l\rec_client.log" -PassThru
Start-Sleep 8
Record $c "lagcomp_150.mp4" 34
Get-Process UnrealEditor -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 3

# 2) Kill cam in the arena
$c = Start-Process $ue -ArgumentList "$p /Game/HitregLab/Maps/Lvl_Arena -game -windowed -ResX=1280 -ResY=720 -WinX=40 -WinY=40 -nosound -nosteam -abslog=$l\rec_arena.log" -PassThru
Start-Sleep 12
Record $c "killcam.mp4" 30
Get-Process UnrealEditor -ErrorAction SilentlyContinue | Stop-Process -Force
"done"

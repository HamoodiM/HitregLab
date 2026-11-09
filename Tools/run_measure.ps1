# Hitreg Lab: runs one unattended measurement (dedicated server + clients) and waits for it to finish.
#   cover  : behind-cover deaths sweep on Lvl_Peek (server -CoverSweep=N, two clients)
#   rules  : hit-registration bench on Lvl_Range comparing rewind rules (one client -HitregBench=N -BenchRules)
#   demo   : short cover sweep at 150 ms, then the runner's client opens the Shot Autopsy viewer on the hit
# Only processes this script started are stopped. Results land in Saved/Measure.
param(
    [ValidateSet('cover', 'rules', 'demo')][string]$Mode = 'cover',
    [int]$Trials = 12,
    [string]$Extra = '',
    [int]$TimeoutMin = 30
)
$ue = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$p = '"C:\UnrealEngineProjects\HitregLab\HitregLab.uproject"'
$l = 'C:\UnrealEngineProjects\HitregLab\Saved\Logs'
New-Item -ItemType Directory -Force -Path $l | Out-Null
$procs = @()
try {
    if ($Mode -eq 'rules') {
        Remove-Item "$l\m_server.log", "$l\m_c1.log" -ErrorAction SilentlyContinue
        $procs += Start-Process $ue -ArgumentList "$p /Game/HitregLab/Maps/Lvl_Range -server -log -nosteam -abslog=$l\m_server.log" -WindowStyle Minimized -PassThru
        Start-Sleep 15
        $procs += Start-Process $ue -ArgumentList "$p 127.0.0.1 -game -windowed -ResX=640 -ResY=360 -WinX=20 -WinY=40 -nosound -nosteam -HitregBench=$Trials -BenchRules -QuitAfterBench $Extra -abslog=$l\m_c1.log" -PassThru
        $done = 'Hitreg bench done'; $watch = "$l\m_c1.log"
    }
    else {
        Remove-Item "$l\m_server.log", "$l\m_c1.log", "$l\m_c2.log" -ErrorAction SilentlyContinue
        $srvArgs = "-CoverSweep=$Trials -QuitAfterSweep $Extra"
        $c1 = ''
        if ($Mode -eq 'demo') { $srvArgs = "-CoverSweep=$Trials -CoverRtt=150 -CoverConfigs=base $Extra"; $c1 = '-AutopsyDemoAt=60' }
        $procs += Start-Process $ue -ArgumentList "$p /Game/HitregLab/Maps/Lvl_Peek -server -log -nosteam $srvArgs -abslog=$l\m_server.log" -WindowStyle Minimized -PassThru
        Start-Sleep 15
        $res = if ($Mode -eq 'demo') { '-ResX=1280 -ResY=720' } else { '-ResX=480 -ResY=270' }
        $procs += Start-Process $ue -ArgumentList "$p 127.0.0.1 -game -windowed $res -WinX=20 -WinY=40 -nosound -nosteam $c1 -abslog=$l\m_c1.log" -PassThru
        Start-Sleep 5
        $procs += Start-Process $ue -ArgumentList "$p 127.0.0.1 -game -windowed -ResX=480 -ResY=270 -WinX=1320 -WinY=40 -nosound -nosteam -abslog=$l\m_c2.log" -PassThru
        $done = 'Cover sweep done'; $watch = "$l\m_server.log"
        if ($Mode -eq 'demo') { "c1 pid $($procs[1].Id)"; return }
    }
    $deadline = (Get-Date).AddMinutes($TimeoutMin)
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path $watch) -and (Select-String -Path $watch -Pattern $done -Quiet)) { break }
        Start-Sleep 5
    }
    Start-Sleep 4
    Select-String -Path $watch -Pattern $done | Select-Object -Last 1 | ForEach-Object { $_.Line }
}
finally {
    if ($Mode -ne 'demo') {
        foreach ($pr in $procs) { if ($pr -and -not $pr.HasExited) { Stop-Process -Id $pr.Id -Force -ErrorAction SilentlyContinue } }
    }
}

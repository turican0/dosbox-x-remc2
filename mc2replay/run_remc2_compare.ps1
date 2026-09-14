# Plays the same recording in remc2 and compares it, frame by frame, with what the original
# game did in DOSBox (a run made by run_replay.ps1 -Seq).  Uses remc2's own regression
# comparison: add_compare(0x2285FF) reads regressions\sequence-002285FF-*.bin and stops at the
# first byte that differs.
#
#   .\run_remc2_compare.ps1 -Run work\runs\full -Play C:\Users\vesely\Downloads\Level5-mine.dem
#
#   -Run       DOSBox run folder (has frames.txt and regressions\)
#   -Play      the recording that run was made from
#   -Steps     how many frames to compare; defaults to the frames DOSBox recorded.  Never more:
#              past the end of the sequence file remc2 would compare against garbage.
#   -Remc2Dir  folder with remc2.exe and the game data (default: remc2-dev2 x64\Debug)
#
# Outcome, read from remc2's output and log.txt:
#   exit 20              every frame matched (End_thread(20) after the last step)
#   "Compare error ..."  the first difference: region, byte offset and step (frame)
# On a difference remc2 deliberately divides by zero (allert_error), so the process does not
# end cleanly; the script stops it once the difference is in the log.

param(
    [Parameter(Mandatory=$true)][string]$Run,
    [Parameter(Mandatory=$true)][string]$Play,
    [int]$Steps = -1,
    [string]$Remc2Dir = "C:\prenos\remc2-dev2\remc2\x64\Debug",
    [int]$TimeoutSec = 7200
)

$ErrorActionPreference = "Stop"
$root   = $PSScriptRoot
$Run    = (Resolve-Path $Run).Path
$Play   = (Resolve-Path $Play).Path
$exe    = Join-Path $Remc2Dir "remc2.exe"
$config = Join-Path $root "work\remc2-compare-config.json"
$seqDir = Join-Path $Run "regressions"

foreach ($f in @($exe, $config, $seqDir, (Join-Path $Run "frames.txt"))) {
    if (-not (Test-Path $f)) { throw "missing $f" }
}

$recorded = @(Get-Content (Join-Path $Run "frames.txt") | Where-Object { $_ -notmatch '^#' }).Count
if ($Steps -lt 0) { $Steps = $recorded }
if ($Steps -gt $recorded) { throw "DOSBox recorded only $recorded frames, cannot compare $Steps" }

$bytes = [System.IO.File]::ReadAllBytes($Play)
$level = [BitConverter]::ToUInt16($bytes, 16)

# remc2 looks for <exe dir>/<memimages path>regressions/sequence-*.bin, so the path has to be
# relative to the exe and end with a slash.
$from = New-Object Uri ((Resolve-Path $Remc2Dir).Path.TrimEnd('\') + '\')
$to   = New-Object Uri ($Run.TrimEnd('\') + '\')
$memimages = [Uri]::UnescapeDataString($from.MakeRelativeUri($to).ToString())

$log = Join-Path $Remc2Dir "log.txt"
if (Test-Path $log) { Remove-Item -Force $log }

$arguments = @(
    "--mode_test_regressions", "2",
    "--text_output_to_console",
    "--set_level", "$level",
    "--config_file_path", "`"$config`"",
    "--play_file", "`"$Play`"",
    "--memimages_path", "`"$memimages`"",
    "--set_max_regressions_steps", "$Steps"
) -join " "

"remc2:     $exe"
"recording: $Play (level $level)"
"compare:   $Steps frames against $seqDir"
"memimages: $memimages"

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = $arguments
$psi.WorkingDirectory = $Remc2Dir
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true

$sw = [Diagnostics.Stopwatch]::StartNew()
$p = [System.Diagnostics.Process]::Start($psi)
$outTask = $p.StandardOutput.ReadToEndAsync()
$errTask = $p.StandardError.ReadToEndAsync()

$found = $false
while (-not $p.WaitForExit(15000)) {
    if ((Test-Path $log) -and (Select-String -Path $log -Pattern "Compare error" -Quiet)) {
        # The difference is written; what follows is the deliberate division by zero.
        Start-Sleep -Seconds 3
        $found = $true
        try { $p.Kill() } catch {}
        $p.WaitForExit(10000) | Out-Null
        break
    }
    "  {0:n0}s: remc2 still comparing" -f $sw.Elapsed.TotalSeconds
    if ($sw.Elapsed.TotalSeconds -gt $TimeoutSec) {
        Write-Warning "timeout after $TimeoutSec s, stopping remc2"
        try { $p.Kill() } catch {}
        $p.WaitForExit(10000) | Out-Null
        break
    }
}
$sw.Stop()

$stdout = $outTask.Result
$stderr = $errTask.Result
Set-Content -Path (Join-Path $Run "remc2_stdout.txt") -Value $stdout -Encoding utf8
Set-Content -Path (Join-Path $Run "remc2_stderr.txt") -Value $stderr -Encoding utf8
if (Test-Path $log) { Copy-Item $log (Join-Path $Run "remc2_log.txt") -Force }

"exit={0} time={1:n1}s" -f $p.ExitCode, $sw.Elapsed.TotalSeconds
$errors = @()
foreach ($text in @($stdout, $stderr)) {
    $errors += ($text -split "`n" | Where-Object { $_ -match "Compare error" })
}
if (Test-Path $log) { $errors += (Get-Content $log | Where-Object { $_ -match "Compare error" }) }
if ($errors.Count -gt 0) {
    "--- FIRST DIFFERENCE ---"
    $errors | Select-Object -Unique
    "--- remc2 output around it ---"
    ($stdout -split "`n") | Where-Object { $_ -match "buffer\[|Compare error" } | Select-Object -First 30
}
elseif ($p.ExitCode -eq 20) {
    "all $Steps frames match"
}
else {
    "no difference reported, but remc2 did not finish the comparison either - see remc2_*.txt"
}

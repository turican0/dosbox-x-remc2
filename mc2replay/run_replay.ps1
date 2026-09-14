# Replays a remc2 recording in the ORIGINAL game under DOSBox-X and writes down what the game
# did, frame by frame, so it can be compared with remc2 playing the same recording.
#
#   .\run_replay.ps1 -Play C:\Users\vesely\Downloads\Level5-mine.dem -Frames 5410 -Seq
#
#   -Play     recording (.dem/.bin, both remc2 layouts - with or without spells)
#   -Level    levelnumber_43w, 0-based.  Taken from the recording when left out
#             (Level5-mine.dem stores 4).
#   -Frames   how many frames of the game loop to record (EIP 0x2285FF)
#   -Seq      also write regressions/sequence-002285FF-*.bin, the files remc2 regression
#             comparison reads (~750 KB per frame)
#   -Dump     raw bytes of the watched regions for every frame (for tools that diff dumps)
#   -Tag      name of the output folder; derived from the recording when left out
#
# Output: work\runs\<tag>\frames.txt - one line per frame (RNG, game clock, CRC32 of the maps
# and of D41A0) plus '#' lines for every stage the run went through: intro skipped, level
# chosen, recording loaded, spells applied, first recorded input, end of the game.
# DOSBox closes on its own when the frames are recorded or the game ends.

param(
    [Parameter(Mandatory=$true)][string]$Play,
    [int]$Level = -1,
    [int]$Frames = 500,
    [string]$Tag = "",
    [switch]$Seq,
    [switch]$Dump,
    [switch]$NoPlayback,   # recording only for the level number; the game gets no input
    [switch]$NoInputs,     # play the recording without the player inputs
    [switch]$NoSpells,     # play the recording without the spells
    [int]$TimeoutSec = 3600,
    [string]$Conf = "",     # DOSBox config; mc2replay.conf when left out
    [string]$Watch = "",    # linear address (hex) whose every change is logged with its EIP
    [int]$WatchSize = 1,
    [int]$WatchFrom = 0,    # log changes only from this frame on
    [string]$Trace = "",    # EIPs (hex, comma separated) where registers and [eax]/[ecx] are logged
    [int]$TraceFrame = -1,  # only in this frame
    [string]$TraceEax = "", # only when EAX holds this value (hex)
    [string]$Poke = "",     # "addr=value,..." (hex bytes) written once when the level is chosen
    [switch]$RawStagePtr    # let sub_12780 dereference stale StageVars2 offsets as the original does
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$exe  = Join-Path (Split-Path -Parent $root) "bin\x64\Release\dosbox-x.exe"
$conf = if ($Conf) { (Resolve-Path $Conf).Path } else { Join-Path $root "mc2replay.conf" }
$game = Join-Path $root "work\game\NETHERW.EXE"

foreach ($f in @($exe, $conf, $game)) {
    if (-not (Test-Path $f)) { throw "missing $f" }
}
$Play = (Resolve-Path $Play).Path

# The recording starts with the 16-byte signature, then the level of its first block.
$bytes = [System.IO.File]::ReadAllBytes($Play)
if ([System.Text.Encoding]::ASCII.GetString($bytes, 0, 16) -ne "MC2-HD-Recording") {
    throw "$Play is not a remc2 recording"
}
$recordedLevel = [BitConverter]::ToUInt16($bytes, 16)
if ($Level -lt 0) { $Level = $recordedLevel }
elseif ($Level -ne $recordedLevel) {
    Write-Warning "the recording starts with level $recordedLevel, running level $Level"
}

if (-not $Tag) {
    $Tag = "{0}_L{1}_{2}f" -f [IO.Path]::GetFileNameWithoutExtension($Play), $Level, $Frames
}
$runDir = Join-Path $root "work\runs\$Tag"
if (Test-Path $runDir) { Remove-Item -Recurse -Force $runDir }
New-Item -ItemType Directory $runDir | Out-Null
$outFile = Join-Path $runDir "frames.txt"

$env:MC2CHK        = "1"
$env:MC2CHK_LEVEL  = "$Level"
$env:MC2CHK_FRAMES = "$Frames"
$env:MC2CHK_OUT    = $outFile
$env:MC2CHK_PLAY   = if ($NoPlayback) { "" } else { $Play }
$env:MC2CHK_NOINPUT  = if ($NoInputs) { "1" } else { "0" }
$env:MC2CHK_NOSPELLS = if ($NoSpells) { "1" } else { "0" }
$env:MC2CHK_SEQ    = ""
$env:MC2CHK_WATCH  = $Watch
$env:MC2CHK_WATCH_SIZE = "$WatchSize"
$env:MC2CHK_WATCH_FROM = "$WatchFrom"
$env:MC2CHK_TRACE  = $Trace
$env:MC2CHK_TRACE_FRAME = "$TraceFrame"
$env:MC2CHK_TRACE_EAX = $TraceEax
$env:MC2CHK_POKE   = $Poke
$env:MC2CHK_RAWSTAGEPTR = if ($RawStagePtr) { "1" } else { "0" }
$env:MC2CHK_DUMP   = ""
if ($Seq) {
    $seqDir = Join-Path $runDir "regressions"
    New-Item -ItemType Directory $seqDir | Out-Null
    $env:MC2CHK_SEQ = $seqDir
}
if ($Dump) {
    $dumpDir = Join-Path $runDir "dump"
    New-Item -ItemType Directory $dumpDir | Out-Null
    $env:MC2CHK_DUMP = $dumpDir
}

"replay: $Play"
"level $Level, $Frames frames -> $runDir"

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = '-conf "' + $conf + '"'
$psi.WorkingDirectory = $runDir
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true

$sw = [Diagnostics.Stopwatch]::StartNew()
$p = [System.Diagnostics.Process]::Start($psi)
$outTask = $p.StandardOutput.ReadToEndAsync()
$errTask = $p.StandardError.ReadToEndAsync()

# Progress while it runs: the number of recorded frames, every 30 s.
while (-not $p.WaitForExit(30000)) {
    $done = 0
    if (Test-Path $outFile) {
        $done = @(Get-Content $outFile | Where-Object { $_ -notmatch '^#' }).Count
    }
    "  {0:n0}s: {1} of {2} frames" -f $sw.Elapsed.TotalSeconds, $done, $Frames
    if ($sw.Elapsed.TotalSeconds -gt $TimeoutSec) {
        Write-Warning "timeout after $TimeoutSec s, stopping DOSBox"
        try { $p.Kill() } catch {}
        $p.WaitForExit(10000) | Out-Null
        break
    }
}
$sw.Stop()
Set-Content -Path (Join-Path $runDir "stdout.txt") -Value $outTask.Result -Encoding utf8
Set-Content -Path (Join-Path $runDir "stderr.txt") -Value $errTask.Result -Encoding utf8

$frameLines = 0
if (Test-Path $outFile) {
    $frameLines = @(Get-Content $outFile | Where-Object { $_ -notmatch '^#' }).Count
}
"exit={0} frames={1} time={2:n1}s" -f $p.ExitCode, $frameLines, $sw.Elapsed.TotalSeconds
if (Test-Path $outFile) {
    "--- what the run went through ---"
    Get-Content $outFile | Where-Object { $_ -match '^#' }
}

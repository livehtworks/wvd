$ErrorActionPreference = 'Stop'
$ValidationRoot = Split-Path $PSScriptRoot -Parent
function Invoke-Evidence {
    param([string]$Exe, [string[]]$Arguments, [string]$Case = 'discovery', [int]$Timeout = 30)
    $folder = Join-Path $ValidationRoot "runs/$Case"
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    $id = [guid]::NewGuid().ToString('N')
    $out = Join-Path $folder "$id.stdout.log"
    $err = Join-Path $folder "$id.stderr.log"
    $start = [DateTime]::UtcNow
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Exe
    $psi.WorkingDirectory = $ValidationRoot
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    foreach ($arg in $Arguments) { $psi.ArgumentList.Add($arg) }
    $proc = [Diagnostics.Process]::new()
    $proc.StartInfo = $psi
    [void]$proc.Start()
    $ot = $proc.StandardOutput.ReadToEndAsync()
    $et = $proc.StandardError.ReadToEndAsync()
    $timedOut = -not $proc.WaitForExit($Timeout * 1000)
    if ($timedOut) { $proc.Kill(); $proc.WaitForExit() }
    [IO.File]::WriteAllText($out, $ot.Result, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($err, $et.Result, [Text.UTF8Encoding]::new($false))
    $exitCode = $proc.ExitCode
    $record = [ordered]@{ command=@($Exe)+$Arguments; cwd=$ValidationRoot; start_utc=$start.ToString('o'); end_utc=[DateTime]::UtcNow.ToString('o'); elapsed_ms=([DateTime]::UtcNow-$start).TotalMilliseconds; exit_code=$exitCode; timed_out=$timedOut; stdout=$out; stderr=$err; phase=$Case }
    $record | ConvertTo-Json -Compress -Depth 5 | Add-Content (Join-Path $ValidationRoot 'private/commands.jsonl') -Encoding UTF8
    $proc.Dispose()
    [pscustomobject]@{ ExitCode=$exitCode; TimedOut=$timedOut; Output=$ot.Result; Error=$et.Result; Log=$out }
}

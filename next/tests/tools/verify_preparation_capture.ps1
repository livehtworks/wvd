#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RepoRoot,[Parameter(Mandatory)][string]$EvidenceRoot,
      [Parameter(Mandatory)][string]$TestApplication,[Parameter(Mandatory)][string]$Pack,
      [Parameter(Mandatory)][string]$Quest,[Parameter(Mandatory)][string]$Profile,
      [Parameter(Mandatory)][string]$BaselinePublication)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'PREPARATION_CAPTURE_REQUIRES_ADMINISTRATOR'}
$root=[IO.Path]::GetFullPath($EvidenceRoot)
$repo=[IO.Path]::GetFullPath($RepoRoot)
if(Test-Path -LiteralPath $root){throw 'FRESH_PREPARATION_EVIDENCE_REQUIRED'}
if(-not $root.StartsWith((Join-Path $repo 'next/.local')+[IO.Path]::DirectorySeparatorChar,'OrdinalIgnoreCase')){throw 'ISOLATED_PREPARATION_ROOT_REQUIRED'}
Import-Module (Join-Path $repo 'next/tools/memory_trace_support.psm1') -Force
Initialize-TraceInterop
Assert-NoExistingWprCapture
if([WvdTraceNative]::SystemCommitRatio() -ge 0.98){throw 'PREPARATION_SYSTEM_COMMIT_BUDGET_UNAVAILABLE'}
[IO.Directory]::CreateDirectory($root) | Out-Null
[xml]$profileXml=Get-Content -Encoding utf8 -Raw (Join-Path $repo 'next/tools/profiles/memory-stacks.wprp')
foreach($node in @($profileXml.SelectNodes('//*[@Value="VirtualAllocation"]'))){[void]$node.ParentNode.RemoveChild($node)}
foreach($node in @($profileXml.SelectNodes('//Stacks[not(*)]'))){[void]$node.ParentNode.RemoveChild($node)}
$profilePath=Join-Path $root 'capture.wprp';$profileXml.Save($profilePath)
$deadline=[DateTime]::UtcNow.AddSeconds(240)
function Command([string[]]$Arguments,[string]$Name,[int]$Seconds=20){
    Invoke-TraceWpr -Arguments $Arguments -Log (Join-Path $root ($Name+'.log')) -TimeoutSeconds $Seconds -DeadlineUtc $deadline
}
$process=$null;$state=$null;$stdout=$null;$stderr=$null
$receipt=@{complete=$false;game_inputs=0;tracking='PID HeapSnapshots';max_seconds=240;max_preparation_wait_seconds=120;
    scope='Isolated production Giant assembly/publication only; no coordinator.start or game/device operations'}
$receipt.application_path=[IO.Path]::GetFullPath($TestApplication)
$receipt.application_sha256=(Get-FileHash -LiteralPath $TestApplication).Hash
$receipt.profile_sha256=(Get-FileHash -LiteralPath $Profile).Hash
$receipt.capture_profile_sha256=(Get-FileHash -LiteralPath $profilePath).Hash
$receipt.baseline_identity_sha256=(Get-FileHash -LiteralPath (Join-Path $BaselinePublication 'program/identity.json')).Hash
try {
    $info=[Diagnostics.ProcessStartInfo]::new([IO.Path]::GetFullPath($TestApplication))
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $info.Environment['WVD_PREPARATION_GATE']='1'
    $data=Join-Path $root 'data';$result=Join-Path $root 'prepare-result.json'
    foreach($argument in @('--publication-prepare',$Pack,$Quest,$Profile,$BaselinePublication,$result,$data)){$info.ArgumentList.Add($argument)}
    $process=[Diagnostics.Process]::Start($info)
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    $ready=Join-Path $data 'capture-ready';$readyDeadline=[DateTime]::UtcNow.AddSeconds(30)
    while(-not (Test-Path -LiteralPath $ready)) {
        if($process.HasExited -or [DateTime]::UtcNow -gt $readyDeadline){throw 'PREPARATION_FIXTURE_NOT_READY'}
        Start-Sleep -Milliseconds 50
    }
    $target=@{pid=$process.Id;process_start_filetime=$process.StartTime.ToFileTimeUtc().ToString()}
    $state=@{session='WvdPrepare_'+[guid]::NewGuid().ToString('N');root=$root;target=$target;
        snapshot_attempted=$false;trace_attempted=$false;stop_attempted=$false}
    $receipt.target=$target;$receipt.session=$state.session
    $state.snapshot_attempted=$true
    Command @('-snapshotconfig','heap','-pid',[string]$target.pid,'enable') 'enable' | Out-Null
    $state.trace_attempted=$true
    Command @('-start',($profilePath+'!WvdMemoryStacks'),'-filemode','-recordtempto',$root,'-instancename',$state.session) 'start' | Out-Null
    [IO.File]::WriteAllText((Join-Path $data 'capture-start'),'')
    $wait=[Diagnostics.Stopwatch]::StartNew()
    $receipt.peak_private_bytes=0L
    while(-not $process.WaitForExit(200)) {
        $process.Refresh()
        $receipt.peak_private_bytes=[Math]::Max($receipt.peak_private_bytes,$process.PrivateMemorySize64)
        if($receipt.peak_private_bytes -gt 1GB){throw 'PREPARATION_PRIVATE_COMMIT_GUARD'}
        if([WvdTraceNative]::SystemCommitRatio() -ge 0.98){throw 'PREPARATION_SYSTEM_COMMIT_GUARD'}
        if($wait.Elapsed.TotalSeconds -ge 120){throw 'TRACKED_PREPARATION_EXCEEDED_120_SECONDS'}
    }
    $receipt.child_elapsed_seconds=$wait.Elapsed.TotalSeconds
    if((Get-FileHash -LiteralPath $TestApplication).Hash -ne $receipt.application_sha256 -or
       (Get-FileHash -LiteralPath $Profile).Hash -ne $receipt.profile_sha256){throw 'PREPARATION_FROZEN_INPUT_CHANGED'}
    if($process.ExitCode -ne 0){throw ('TRACKED_PREPARATION_FAILED:'+ $process.ExitCode)}
    $measurement=Get-Content -Encoding utf8 -Raw -LiteralPath $result | ConvertFrom-Json
    $receipt.measurement=$measurement
    $receipt.complete=[bool]$measurement.passed
} catch {$receipt.failure=$_.Exception.Message} finally {
    if($process -and -not $process.HasExited){$process.Kill();if(-not $process.WaitForExit(5000)){$receipt.failure='PREPARATION_FIXTURE_EXIT_UNCONFIRMED';$receipt.complete=$false}}
    if($state){$receipt.cleanup=Close-OwnedTraceCapture $state ${function:Command};if(-not $receipt.cleanup.cleanup_confirmed){$receipt.complete=$false}}
    if($stdout){$stdout.GetAwaiter().GetResult() | Set-Content -Encoding utf8 (Join-Path $root 'application.stdout.log')}
    if($stderr){$stderr.GetAwaiter().GetResult() | Set-Content -Encoding utf8 (Join-Path $root 'application.stderr.log')}
    $receipt | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $root 'receipt.json')
    if($process){$process.Dispose()}
}
if(-not $receipt.complete){exit 1}

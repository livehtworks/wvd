#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RepoRoot,[Parameter(Mandatory)][string]$EvidenceRoot,
      [Parameter(Mandatory)][string]$AnalyzerPath,[Parameter(Mandatory)][string]$FixturePath,
      [switch]$OnlyKnownAllocations,[switch]$OnlyOwnershipCases)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath($RepoRoot)
$root=[IO.Path]::GetFullPath($EvidenceRoot)
if (-not $root.StartsWith((Join-Path $repo 'next/.local')+[IO.Path]::DirectorySeparatorChar,'OrdinalIgnoreCase') -or (Test-Path $root)) { throw 'NEW_ISOLATED_EVIDENCE_ROOT_REQUIRED' }
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'FIXTURE_CAPTURE_REQUIRES_ADMINISTRATOR' }
Import-Module (Join-Path $repo 'next/tools/memory_trace_support.psm1') -Force
Initialize-TraceInterop
Assert-NoExistingWprCapture
New-Item -ItemType Directory -Path $root | Out-Null
$control=Join-Path $root 'control'
New-Item -ItemType Directory -Path $control | Out-Null
$deadline=[DateTime]::UtcNow.AddSeconds(120)
$script:activeRoot=$root
$profilePath=Join-Path $root 'heap-only.wprp'
[xml]$profile=Get-Content -Encoding utf8 -Raw (Join-Path $repo 'next/tools/profiles/memory-stacks.wprp')
foreach($node in @($profile.SelectNodes('//*[@Value="VirtualAllocation"]'))) { [void]$node.ParentNode.RemoveChild($node) }
foreach($node in @($profile.SelectNodes('//*[local-name()="Stacks"]'))) {if(-not $node.HasChildNodes){[void]$node.ParentNode.RemoveChild($node)}}
$profile.Save($profilePath)
$profileId=$profilePath+'!WvdMemoryStacks'
function Command([string[]]$Arguments,[string]$Name,[int]$Seconds=20) {
    Invoke-TraceWpr -Arguments $Arguments -Log (Join-Path $script:activeRoot ($Name+'.log')) -TimeoutSeconds $Seconds -DeadlineUtc $deadline
}
function Check([bool]$Value,[string]$Failure) {if(-not $Value){throw $Failure}}
function Wait-Ready([string]$Name) {
    $end=[DateTime]::UtcNow.AddSeconds(10)
    while(-not (Test-Path (Join-Path $control $Name))) {
        if($fixture.HasExited -or [DateTime]::UtcNow -gt $end){throw ('FIXTURE_NOT_READY:'+ $Name)}
        Start-Sleep -Milliseconds 20
    }
}
function State([string]$Name,[bool]$Snapshot,[bool]$Trace) {
    $script:activeRoot=Join-Path $root $Name
    New-Item -ItemType Directory -Path $script:activeRoot | Out-Null
    return @{session='WvdFixture_'+[guid]::NewGuid().ToString('N');root=$script:activeRoot;
        target=$target;snapshot_attempted=$Snapshot;trace_attempted=$Trace;stop_attempted=$false}
}
$fixture=$null;$state=$null
$results=[Collections.Generic.List[object]]::new()
try {
    $fixture=Start-Process -FilePath $FixturePath -ArgumentList @('"'+$control+'"') -WindowStyle Hidden -PassThru
    Wait-Ready 'ready'
    $target=[pscustomobject]@{pid=$fixture.Id;process_start_filetime=$fixture.StartTime.ToFileTimeUtc().ToString()}
    Check ((Command @('-snapshotconfig','heap','-pid',[string]$target.pid) 'before') -match 'snapshot is disabled') 'FIXTURE_SNAPSHOT_PRESTATE_NOT_DISABLED'

    if(-not $OnlyKnownAllocations) {
    $state=State 'enable-after-log-error' $true $false
    $badLog=Join-Path $state.root 'log-is-directory'; New-Item -ItemType Directory $badLog | Out-Null
    $failed=$false
    try {Invoke-TraceWpr -Arguments @('-snapshotconfig','heap','-pid',[string]$target.pid,'enable') -Log $badLog -DeadlineUtc $deadline | Out-Null}
    catch {$failed=$true}
    Check $failed 'LOG_ERROR_NOT_OBSERVED'
    Check ((Command @('-snapshotconfig','heap','-pid',[string]$target.pid) 'actual-after-error') -match 'snapshot is enabled') 'ENABLE_NOT_ACTUALLY_APPLIED'
    $cleanup=Close-OwnedTraceCapture $state ${function:Command}
    Check $cleanup.cleanup_confirmed 'ENABLE_AFTER_ERROR_NOT_CLEANED'
    $results.Add(@{case='enable_applied_then_log_failed';passed=$true;cleanup=$cleanup})

    $state=State 'start-after-log-error' $false $true
    $badLog=Join-Path $state.root 'log-is-directory'; New-Item -ItemType Directory $badLog | Out-Null
    $failed=$false
    try {Invoke-TraceWpr -Arguments @('-start',$profileId,'-filemode','-recordtempto',$state.root,'-instancename',$state.session) -Log $badLog -DeadlineUtc $deadline | Out-Null}
    catch {$failed=$true}
    Check ($failed -and @([WvdTraceNative]::Discover($state.session)).Count -gt 0) 'START_EFFECT_AFTER_ERROR_NOT_OBSERVED'
    $cleanup=Close-OwnedTraceCapture $state ${function:Command}
    Check $cleanup.cleanup_confirmed 'START_AFTER_ERROR_NOT_CLEANED'
    $results.Add(@{case='start_applied_then_log_failed';passed=$true;cleanup=$cleanup})

    $state=State 'start-after-timeout' $false $true
    $startedMarker=Join-Path $state.root 'start-applied'
    $wpr=Join-Path $env:WINDIR 'System32/wpr.exe'
    $command="& '$wpr' -start '$profileId' -filemode -recordtempto '$($state.root)' -instancename '$($state.session)'; if(`$LASTEXITCODE -ne 0){exit 1}; [IO.File]::WriteAllText('$startedMarker',''); Start-Sleep -Seconds 60"
    $failed=$false
    try {Invoke-TraceTool -Executable (Join-Path $PSHOME 'pwsh.exe') -Arguments @('-NoProfile','-Command',$command) -Log (Join-Path $state.root 'timeout.log') -TimeoutSeconds 5 -DeadlineUtc $deadline | Out-Null}
    catch {$failed=$_.Exception.Message -match 'TRACE_TOOL_TIMEOUT'}
    Check ($failed -and (Test-Path $startedMarker) -and @([WvdTraceNative]::Discover($state.session)).Count -gt 0) 'START_APPLIED_TIMEOUT_NOT_OBSERVED'
    $cleanup=Close-OwnedTraceCapture $state ${function:Command}
    Check $cleanup.cleanup_confirmed 'START_TIMEOUT_NOT_CLEANED'
    $results.Add(@{case='start_applied_then_timeout';passed=$true;cleanup=$cleanup})

    $state=State 'foreign-instance' $true $true
    Command @('-snapshotconfig','heap','-pid',[string]$target.pid,'enable') 'enable' | Out-Null
    Command @('-start',$profileId,'-filemode','-recordtempto',$state.root,'-instancename',$state.session) 'start' | Out-Null
    $rejected=$false; try {Assert-NoExistingWprCapture} catch {$rejected=$true}
    Check $rejected 'FOREIGN_NAMED_INSTANCE_NOT_REJECTED'
    $unowned=@{}+$state; $unowned.session='WvdFixtureNeverStarted'; $unowned.trace_attempted=$false; $unowned.snapshot_attempted=$false
    $unused=Close-OwnedTraceCapture $unowned ${function:Command}
    Check (@([WvdTraceNative]::Discover($state.session)).Count -gt 0) 'UNOWNED_NAMED_TRACE_TOUCHED'
    $reused=@{}+$unowned; $reused.snapshot_attempted=$true
    $reused.target=[pscustomobject]@{pid=$target.pid;process_start_filetime=([long]$target.process_start_filetime-1).ToString()}
    $guard=Close-OwnedTraceCapture $reused ${function:Command}
    Check (-not $guard.cleanup_confirmed -and $guard.errors -contains 'PID_IDENTITY_CHANGED_CONFIG_NOT_TOUCHED') 'PID_CREATION_GUARD_NOT_APPLIED'
    Check ((Command @('-snapshotconfig','heap','-pid',[string]$target.pid) 'foreign-config-still-enabled') -match 'snapshot is enabled') 'REUSED_PID_CONFIG_TOUCHED'
    $cleanup=Close-OwnedTraceCapture $state ${function:Command}
    Check $cleanup.cleanup_confirmed 'FOREIGN_FIXTURE_CLEANUP_FAILED'
    $results.Add(@{case='foreign_named_trace_and_pid_creation_change';passed=$true;cleanup=$cleanup})
    }
    if($OnlyOwnershipCases){$results | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $root 'results.json');return}

    $state=State 'known-allocations' $true $true
    Command @('-snapshotconfig','heap','-pid',[string]$target.pid,'enable') 'enable' | Out-Null
    Command @('-start',$profileId,'-filemode','-recordtempto',$state.root,'-instancename',$state.session) 'start' | Out-Null
    [IO.File]::WriteAllText((Join-Path $control 'b0.request'),'')
    Wait-Ready 'b0.ready'
    Command @('-singlesnapshot','heap',[string]$target.pid,'-instancename',$state.session) 'b0' 20 | Out-Null
    [IO.File]::WriteAllText((Join-Path $control 'b1.request'),'')
    Wait-Ready 'b1.ready'
    Command @('-singlesnapshot','heap',[string]$target.pid,'-instancename',$state.session) 'b1' 20 | Out-Null
    $etl=Join-Path $state.root 'fixture.etl'
    $state.stop_attempted=$true
    Command @('-stop',$etl,'-skipPdbGen','-compress','-instancename',$state.session) 'stop' 60 | Out-Null
    $cleanup=Close-OwnedTraceCapture $state ${function:Command}
    Check $cleanup.cleanup_confirmed 'KNOWN_CAPTURE_CLEANUP_FAILED'
    $analysis=Join-Path $state.root 'analysis'
    Invoke-TraceTool -Executable $AnalyzerPath -Arguments @('analyze','--etl',$etl,'--pid',[string]$target.pid,
        '--target-filetime',$target.process_start_filetime,'--target-image','heap_fixture.exe','--output',$analysis,
        '--pdb-directory',[IO.Path]::GetDirectoryName($FixturePath),'--memory-mib','1024','--output-mib','128') `
        -Log (Join-Path $state.root 'analysis.log') -TimeoutSeconds 120 -OutputRoot $analysis | Out-Null
    $dictionary=@{}; Get-Content -Encoding utf8 (Join-Path $analysis 'stack-dictionary.jsonl') | ForEach-Object {
        $row=$_ | ConvertFrom-Json; $dictionary[$row.stack_key]=$row.full_stack
    }
    $delta=Import-Csv -Encoding utf8 (Join-Path $analysis 'heap-delta-by-stack.csv')
    foreach($spec in @(@('AllocKeep',4,16384),@('AllocRelease',-3,-6144),@('AllocStable',0,0),@('AllocChurn',0,0))) {
        $rows=@($delta | Where-Object {$dictionary[$_.stack_key] -match $spec[0]})
        $blocks=0L;$bytes=0L
        foreach($row in $rows){$blocks+=[long]$row.delta_blocks;$bytes+=[long]$row.delta_bytes}
        Check ($blocks -eq $spec[1] -and $bytes -eq $spec[2]) ('KNOWN_DIFF_MISMATCH:'+ $spec[0]+':'+$blocks+':'+$bytes)
        $results.Add(@{case=$spec[0];passed=$true;delta_blocks=$blocks;delta_bytes=$bytes})
    }
    $results | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $root 'results.json')
} catch {
    @{complete=$false;failure=$_.Exception.Message;completed_cases=@($results)} | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $root 'failure.json')
    throw
} finally {
    if($state){$script:activeRoot=$state.root; Close-OwnedTraceCapture $state ${function:Command} | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $root 'cleanup-final.json')}
    if($fixture){[IO.File]::WriteAllText((Join-Path $control 'stop.request'),''); if(-not $fixture.WaitForExit(5000)){$fixture.Kill();[void]$fixture.WaitForExit(5000)}; $fixture.Dispose()}
}

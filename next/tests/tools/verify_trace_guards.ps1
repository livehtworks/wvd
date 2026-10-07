#requires -Version 7.0
param([Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(Test-Path $EvidenceRoot){throw 'NEW_ISOLATED_ROOT_REQUIRED'}
New-Item -ItemType Directory -Path $EvidenceRoot | Out-Null
Import-Module (Join-Path $PSScriptRoot '../../tools/memory_trace_support.psm1') -Force
$results=[Collections.Generic.List[object]]::new()
function Reject([string]$Case,[scriptblock]$Action,[string]$Pattern) {
    $failure=''
    try {& $Action} catch {$failure=$_.Exception.Message}
    if(-not $failure -or $failure -notmatch $Pattern){throw "GUARD_NOT_REJECTED:${Case}:$failure"}
    $results.Add(@{case=$Case;passed=$true;failure=$failure})
}
$before=[pscustomobject]@{state='Completed';quiescent=$true;busy=$true;run_id=1;generation=1;
    service_instance='service-A';run_directory='D:/isolated/runs/instance/1';
    repeat=[pscustomobject]@{active=$true;state='waiting';request_id='batch-A';completed_cycles=1}}
Assert-TraceSnapshotBoundary $before $before 'worker_joined'
$results.Add(@{case='busy_batch_waiting_is_legal';passed=$true})
$after=$before | ConvertTo-Json -Depth 5 | ConvertFrom-Json
$after.repeat.state='starting'
Reject 'waiting_to_starting_old_coordinator_fields' {Assert-TraceSnapshotBoundary $before $after 'worker_joined'} 'CROSSED_PREPARATION'
$after=$before | ConvertTo-Json -Depth 5 | ConvertFrom-Json
$after.repeat.request_id='batch-B'
Reject 'batch_changed' {Assert-TraceSnapshotBoundary $before $after 'worker_joined'} 'IDENTITY_CHANGED'
$after=$before | ConvertTo-Json -Depth 5 | ConvertFrom-Json
$after.generation=2
Reject 'generation_changed' {Assert-TraceSnapshotBoundary $before $after 'worker_joined'} 'IDENTITY_CHANGED'
$released=$before | ConvertTo-Json -Depth 5 | ConvertFrom-Json
$released.busy=$false;$released.repeat.active=$false;$released.repeat.state='completed'
Assert-TraceSnapshotBoundary $released $released 'batch_payloads_released'
Reject 'worker_join_to_batch_release' {Assert-TraceSnapshotBoundary $before $released 'worker_joined'} 'CROSSED_PREPARATION'
$user=$released | ConvertTo-Json -Depth 5 | ConvertFrom-Json
$user.repeat.request_id='new-user-task';$user.busy=$true
Reject 'new_user_task_during_collect' {Assert-TraceSnapshotBoundary $released $user 'batch_payloads_released'} 'NOT_RELEASED|IDENTITY_CHANGED'
Reject 'successful_stop_text_nonzero_final_loss' {Assert-TraceFileIntegrity ([pscustomobject]@{
    inspection_succeeded=$true;complete=$true;events_lost=1;buffers_lost=0})} 'EVENTS_OR_BUFFERS_LOST'
Reject 'unknown_final_statistics' {Assert-TraceFileIntegrity ([pscustomobject]@{
    inspection_succeeded=$true;complete=$true;events_lost=$null;buffers_lost=$null})} 'UNKNOWN_OR_INCOMPLETE'
Reject 'unfinished_final_file' {Assert-TraceFileIntegrity ([pscustomobject]@{
    inspection_succeeded=$true;complete=$false;events_lost=0;buffers_lost=0})} 'UNKNOWN_OR_INCOMPLETE'

$pwsh=Join-Path $PSHOME 'pwsh.exe'
function Child-Sleep([string]$Marker) {
    return @('-NoProfile','-Command',"[IO.File]::WriteAllText('$Marker',[string]`$PID); Start-Sleep -Seconds 60")
}
function Child-Gone([string]$Marker) {
    if(-not (Test-Path $Marker)){throw 'CHILD_START_NOT_OBSERVED'}
    $id=[int](Get-Content -Encoding utf8 $Marker)
    if(Get-Process -Id $id -ErrorAction SilentlyContinue){throw "OWN_CHILD_STILL_RUNNING:$id"}
}
$marker=Join-Path $EvidenceRoot 'timeout.pid'
Reject 'owned_child_timeout' {Invoke-TraceTool -Executable $pwsh -Arguments (Child-Sleep $marker) `
    -Log (Join-Path $EvidenceRoot 'timeout.log') -TimeoutSeconds 2} 'TRACE_TOOL_TIMEOUT'
Child-Gone $marker
$marker=Join-Path $EvidenceRoot 'deadline.pid'
Reject 'absolute_deadline' {Invoke-TraceTool -Executable $pwsh -Arguments (Child-Sleep $marker) `
    -Log (Join-Path $EvidenceRoot 'deadline.log') -TimeoutSeconds 60 -DeadlineUtc ([DateTime]::UtcNow.AddSeconds(2))} 'ABSOLUTE_DEADLINE'
Child-Gone $marker
$marker=Join-Path $EvidenceRoot 'cancel.pid'
$cancelAfter=[DateTime]::UtcNow.AddSeconds(2)
Reject 'owned_child_cancel' {Invoke-TraceTool -Executable $pwsh -Arguments (Child-Sleep $marker) `
    -Log (Join-Path $EvidenceRoot 'cancel.log') -TimeoutSeconds 60 -Cancelled { [DateTime]::UtcNow -ge $cancelAfter }} 'TRACE_TOOL_CANCELLED'
Child-Gone $marker

$module=Get-Module memory_trace_support
$saved=& $module {(Get-Command Get-TraceFileBytes).ScriptBlock}
$marker=Join-Path $EvidenceRoot 'metadata-error.pid'
try {
    & $module {param($path) $script:faultMarker=$path
        function script:Get-TraceFileBytes {param($Root,$Filter) if(Test-Path $script:faultMarker){throw 'INJECTED_LENGTH_QUERY_ERROR'};return 0L}
    } $marker
    Reject 'file_query_exception_kills_owned_child' {Invoke-TraceTool -Executable $pwsh -Arguments (Child-Sleep $marker) `
        -Log (Join-Path $EvidenceRoot 'metadata-error.log') -TimeoutSeconds 60 -OutputRoot $EvidenceRoot} 'INJECTED_LENGTH_QUERY_ERROR'
    Child-Gone $marker
} finally {& $module {param($definition) Set-Item Function:Get-TraceFileBytes $definition} $saved}
$marker=Join-Path $EvidenceRoot 'active-output.pid'
$output=Join-Path $EvidenceRoot 'active-output'
New-Item -ItemType Directory $output | Out-Null
$blob=Join-Path $output 'growing.bin'
$writeCommand="[IO.File]::WriteAllText('$marker',[string]`$PID); `$f=[IO.File]::Open('$blob','Create','Write','ReadWrite'); `$b=[byte[]]::new(32768); while(`$true){`$f.Write(`$b);`$f.Flush();Start-Sleep -Milliseconds 10}"
Reject 'active_output_file_limit' {Invoke-TraceTool -Executable $pwsh -Arguments @('-NoProfile','-Command',$writeCommand) `
    -Log (Join-Path $EvidenceRoot 'active-output.log') -TimeoutSeconds 10 -OutputRoot $output -OutputLimit 131072} 'TRACE_EXPORT_OUTPUT_LIMIT'
Child-Gone $marker
Reject 'expired_deadline_does_not_start_child' {Invoke-TraceTool -Executable $pwsh -Arguments @('-NoProfile','-Command','exit 0') `
    -Log (Join-Path $EvidenceRoot 'expired.log') -DeadlineUtc ([DateTime]::UtcNow.AddSeconds(-1))} 'DEADLINE_ALREADY_EXPIRED'
$results | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $EvidenceRoot 'results.json')
Write-Output ('Passed targeted trace guards: '+$results.Count)

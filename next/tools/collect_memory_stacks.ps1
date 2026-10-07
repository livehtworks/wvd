#requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Preflight', 'Probe', 'Collect', 'Monitor')][string]$Mode = 'Preflight',
    [Parameter(Mandatory)][string]$EvidenceRoot,
    [Parameter(Mandatory)][string]$CandidateRoot,
    [Parameter(Mandatory)][string]$DataRoot,
    [Parameter(Mandatory)][string]$PdbPath,
    [string]$AnalyzerPath = (Join-Path $PSScriptRoot 'heap_analyzer/bin/Release/net8.0/HeapAnalyzer.exe'),
    [ValidateRange(1024, 65535)][int]$Port = 17654,
    [string]$TaskRequestPath,
    [string]$BatchRequestId,
    [ValidateSet('HeapAndVirtualAlloc','HeapSnapshots')][string]$CaptureKind = 'HeapAndVirtualAlloc',
    [string]$DeadlineUtc,
    [ValidateRange(0,536870912)][long]$PriorTraceBytes = 0,
    [switch]$MonitorCurrentRound,
    [ValidateRange(0,1200)][int]$EstimatedRoundSeconds=0,
    [switch]$AllowGameTasks
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'memory_trace_support.psm1') -Force
Initialize-TraceInterop
$root = [IO.Path]::GetFullPath($EvidenceRoot)
$data = [IO.Path]::GetFullPath($DataRoot).TrimEnd('\', '/')
$candidate = [IO.Path]::GetFullPath($CandidateRoot)
if ($root.StartsWith($data + [IO.Path]::DirectorySeparatorChar, 'OrdinalIgnoreCase') -or $root -eq $data) {
    throw 'EVIDENCE_MUST_NOT_BE_INSIDE_AUTHORITY_DATA'
}
if (Test-Path -LiteralPath $root) { throw 'EVIDENCE_DIRECTORY_ALREADY_EXISTS' }
if ($Mode -eq 'Collect' -and (-not $AllowGameTasks -or -not $TaskRequestPath)) {
    throw 'COLLECT_REQUIRES_EXPLICIT_GAME_TASK_REQUEST'
}
if ($Mode -eq 'Monitor' -and (-not $BatchRequestId -or $AllowGameTasks -or $TaskRequestPath)) {
    throw 'MONITOR_REQUIRES_EXISTING_BATCH_WITHOUT_TASK_SUBMISSION'
}
if ($MonitorCurrentRound -and $Mode -ne 'Monitor') { throw 'PARTIAL_ROUND_OPTION_REQUIRES_MONITOR' }
New-Item -ItemType Directory -Path $root | Out-Null
$url = "http://127.0.0.1:$Port"
$session = 'WvdStacks_' + [Guid]::NewGuid().ToString('N')
$watch = [Diagnostics.Stopwatch]::StartNew()
$absoluteDeadline = if ($DeadlineUtc) { [DateTimeOffset]::Parse($DeadlineUtc).UtcDateTime } else { [DateTime]::UtcNow.AddMinutes(20) }
if ($absoluteDeadline -gt [DateTime]::UtcNow.AddMinutes(20)) { throw 'CAPTURE_DEADLINE_EXCEEDS_TWENTY_MINUTES' }
$traceOwned = $false
$snapshotOwned = $false
$traceAttempted = $false
$snapshotAttempted = $false
$stopAttempted = $false
$lastServiceCheck=[DateTime]::MinValue
$lastRunRead=[DateTime]::MinValue
$cachedRun=$null
$observerRequests=[Collections.Generic.List[object]]::new()
$names = @()
$requestId = ''
$completed = 0
$failure = ''
$checkpoints = [Collections.Generic.List[object]]::new()
$health = [Collections.Generic.List[object]]::new()
$target = $null
$identity = $null
$mutex = $null
$lockOwned = $false
$result = [ordered]@{ mode=$Mode; complete=$false; allocation_attribution='UNRESOLVED';
    max_rounds=2; max_seconds=1200; max_total_trace_bytes=512MB; rounds_submitted=0; rounds_started=0; rounds_completed=0;
    session=$session; heap_scope='PID; only stacks allocated after enable';
    virtual_alloc_scope='system-wide minimal VirtualAlloc stacks with stack caching; target PID filtering at analysis, not at recording';
    preexisting_allocation_stacks='not covered'; symbols_loaded=$null; cleanup_confirmed=$false }
$result.capture_kind=$CaptureKind
$result.boundary_contract_version=2
$result.events_lost=$null
$result.buffers_lost=$null
$result.final_trace_validated=$false
$result.deadline_utc=$absoluteDeadline.ToString('o')
$result.prior_trace_bytes=$PriorTraceBytes
$result.initial_monitored_round_is_partial=[bool]$MonitorCurrentRound
if ($CaptureKind -eq 'HeapSnapshots') { $result.virtual_alloc_scope='not_collected_heap_snapshot_only' }
function Write-Json([string]$Name, $Value) {
    $Value | ConvertTo-Json -Depth 16 | Set-Content -Encoding utf8 -LiteralPath (Join-Path $root $Name)
}
function Wpr([string[]]$Arguments, [string]$Name, [int]$Seconds=20) {
    $remaining=[int][Math]::Floor(($absoluteDeadline-[DateTime]::UtcNow).TotalSeconds)
    $cleanup=$Name -match '^(stop|cancel-owned|snapshot-disable|snapshot-config-(after|cleanup))'
    $deadline=$absoluteDeadline
    if ($remaining -lt 1) {
        if (-not $cleanup) { throw 'ABSOLUTE_CAPTURE_DEADLINE_EXPIRED' }
        $result.cleanup_deadline_overrun=$true
        $deadline=[DateTime]::UtcNow.AddSeconds([Math]::Min(20,$Seconds))
        $remaining=[Math]::Min(20,$Seconds)
    }
    Invoke-TraceWpr -Arguments $Arguments -Log (Join-Path $root ($Name + '.log')) `
        -TimeoutSeconds ([Math]::Min($Seconds,$remaining)) -DeadlineUtc $deadline `
        -Cancelled $(if($cleanup){$null}else{{Test-Path -LiteralPath (Join-Path $root 'stop.request')}})
}
function Current-Run([switch]$Fresh) {
    if (-not $Fresh -and $cachedRun -and ([DateTime]::UtcNow-$lastRunRead).TotalSeconds -lt 5) { return $cachedRun }
    $began=[DateTime]::UtcNow
    $value=Invoke-RestMethod "$url/api/v1/runs/current" -TimeoutSec 3
    $value | Add-Member -NotePropertyName service_instance -NotePropertyValue $target.instance_id
    $script:lastRunRead=[DateTime]::UtcNow; $script:cachedRun=$value
    $observerRequests.Add(@{began_utc=$began.ToString('o');ended_utc=$lastRunRead.ToString('o');run_id=$value.run_id})
    return $value
}
function Check-Identity([switch]$Fresh) {
    if ($Fresh -or ([DateTime]::UtcNow-$lastServiceCheck).TotalSeconds -ge 5) {
        $live = Invoke-RestMethod "$url/api/v1/service" -TimeoutSec 3
        if ($live.pid -ne $target.pid -or $live.instance_id -ne $target.instance_id -or
            $live.process_start_filetime -ne $target.process_start_filetime -or
            $live.executable -ne $target.executable -or $live.data_root -ne $target.data_root) {
            throw 'TARGET_PROCESS_CHANGED'
        }
        $script:lastServiceCheck=[DateTime]::UtcNow
    }
    $process = Get-Process -Id $target.pid -ErrorAction Stop
    if ($process.Path -ne $target.executable -or
        $process.StartTime.ToFileTimeUtc().ToString() -ne $target.process_start_filetime) { throw 'OS_PROCESS_IDENTITY_CHANGED' }
}
function Check-Health {
    Check-Identity
    $cleanupReserve=180
    if ($watch.Elapsed.TotalSeconds -ge 1200-$cleanupReserve -or [DateTime]::UtcNow -ge $absoluteDeadline.AddSeconds(-$cleanupReserve)) { throw 'TIME_LIMIT_CLEANUP_RESERVED' }
    if (Test-Path -LiteralPath (Join-Path $root 'stop.request')) { throw 'OPERATOR_STOP' }
    $rows = @($names | ForEach-Object { [WvdTraceNative]::Query($_) })
    if ($rows.Count -ne 2) { throw 'COLLECTOR_COVERAGE_MISSING' }
    $total = Get-TraceFileBytes $root
    if ($total+$PriorTraceBytes -ge 512MB) { throw 'AGGREGATE_TRACE_BUDGET_EXCEEDED' }
    $health.Add(@{utc=[DateTime]::UtcNow.ToString('o');elapsed_s=$watch.Elapsed.TotalSeconds;
        trace_bytes=$total;collectors=$rows})
    if (@($rows | Where-Object { $_.EventsLost -gt 0 -or $_.LogBuffersLost -gt 0 -or $_.RealTimeBuffersLost -gt 0 }).Count) {
        throw 'LIVE_ETW_EVENTS_OR_BUFFERS_LOST'
    }
    # Native Sequential limits also bound each collector between polling calls.
    foreach ($row in $rows) {
        $file = [IO.File]::Open($row.File, 'Open', 'Read', [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        try { $length=$file.Length } finally { $file.Dispose() }
        $limit = if ($row.Name -like '*WvdMemory-Snapshot') { 60MB } else { 92MB }
        if ($length -ge $limit) { throw 'COLLECTOR_FILE_BUDGET_HEADROOM' }
    }
    if ($total -ge 152MB) { throw 'RAW_TRACE_BUDGET_HEADROOM' }
    $memory = Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory
    if ($memory.CommittedBytes / $memory.CommitLimit -ge .98) { throw 'SYSTEM_COMMIT_NEAR_EXHAUSTION' }
}
function Snapshot([string]$Phase, $Boundary=$null) {
    Check-Health
    Check-Identity -Fresh
    $runBefore = if ($Boundary) { Current-Run -Fresh } else { $null }
    if ($Boundary) {
        Assert-TraceSnapshotBoundary $runBefore $runBefore $Boundary.stage
        if ($runBefore.run_id -ne $Boundary.run_id -or $runBefore.generation -ne $Boundary.generation -or
            $runBefore.run_directory -ne $Boundary.run_directory -or
            $runBefore.repeat.request_id -ne $Boundary.batch_request_id -or
            $runBefore.repeat.completed_cycles -ne $Boundary.completed_cycles) {
            throw 'SNAPSHOT_EXPECTED_BOUNDARY_CHANGED_BEFORE_COMMAND'
        }
    }
    # No background current requests are issued here. The same bounded silence
    # reduces observer allocations but does not promise that HTTP callbacks died.
    Start-Sleep -Milliseconds 1000
    Check-Health
    $osBefore=Get-Process -Id $target.pid
    $before = [DateTime]::UtcNow.ToString('o')
    $osBeforeSample=@{observed_utc=$before;private_bytes=$osBefore.PrivateMemorySize64;
        pid=$osBefore.Id;process_start_filetime=$osBefore.StartTime.ToFileTimeUtc().ToString()}
    Wpr @('-singlesnapshot', 'heap', [string]$target.pid, '-instancename', $session) ("snapshot-$Phase") 30 | Out-Null
    if ($Boundary) {
        $osAfter=Get-Process -Id $target.pid
        $osAfterSample=@{observed_utc=[DateTime]::UtcNow.ToString('o');private_bytes=$osAfter.PrivateMemorySize64;
            pid=$osAfter.Id;process_start_filetime=$osAfter.StartTime.ToFileTimeUtc().ToString()}
        Check-Identity -Fresh
        $runAfter = Current-Run -Fresh
        try { Assert-TraceSnapshotBoundary $runBefore $runAfter $Boundary.stage }
        catch {
            Write-Json 'rejected-snapshot.json' @{phase=$Phase;before=$runBefore | Select-Object state,busy,quiescent,run_id,run_directory,repeat;
                after=$runAfter | Select-Object state,busy,quiescent,run_id,run_directory,repeat;failure=$_.Exception.Message}
            throw
        }
        $Boundary.before=$runBefore | Select-Object state,busy,quiescent,run_id,generation,service_instance,run_directory,repeat
        $Boundary.after=$runAfter | Select-Object state,busy,quiescent,run_id,generation,service_instance,run_directory,repeat
        $Boundary.os_before=$osBeforeSample
        $Boundary.os_after=$osAfterSample
    }
    $checkpoints.Add(@{phase=$Phase;began_utc=$before;ended_utc=[DateTime]::UtcNow.ToString('o');
        pid=$target.pid;process_start_filetime=$target.process_start_filetime;boundary=$Boundary})
    Check-Health
    Write-Json 'checkpoints.json' $checkpoints
}
function Joined-Boundary($Run, [string]$Stage='worker_joined') {
    $directory=[IO.Path]::GetFullPath($Run.run_directory)
    if (-not $directory.StartsWith((Join-Path $data 'runs') + [IO.Path]::DirectorySeparatorChar, 'OrdinalIgnoreCase')) {
        throw 'RUN_DIRECTORY_OUTSIDE_BOUND_DATA'
    }
    $life=Get-Content -Encoding utf8 -Raw -LiteralPath (Join-Path $directory 'memory-lifecycle.json') | ConvertFrom-Json
    $saved=Get-Content -Encoding utf8 -Raw -LiteralPath (Join-Path $directory 'result.json') | ConvertFrom-Json
    if ($life.failed -ne 0 -or $life.run_id -ne $Run.run_id -or
        $life.instance_id -ne [IO.DirectoryInfo]::new($directory).Parent.Name -or
        $saved.run_id -ne $Run.run_id -or $saved.generation -ne $Run.generation -or
        $saved.state -ne 'Completed' -or -not $saved.quiescent -or -not $saved.details_complete -or
        $saved.secondary_errors.Count -ne 0) { throw 'SAVED_BOUNDARY_IDENTITY_OR_TERMINAL_INVALID' }
    $eventsPath=Join-Path $directory 'execution-events.jsonl'
    if(-not (Test-Path -LiteralPath $eventsPath)){throw 'RECOVERY_HISTORY_NOT_AVAILABLE'}
    $reader=[IO.StreamReader]::new($eventsPath,[Text.Encoding]::UTF8)
    try {
        while($null -ne ($line=$reader.ReadLine())) {
            if([DateTime]::UtcNow -ge $absoluteDeadline.AddSeconds(-180)){throw 'BOUNDARY_HISTORY_READ_EXCEEDED_CAPTURE_WINDOW'}
            $event=$line | ConvertFrom-Json
            if($event.type -like 'recovery.*' -or $event.type -eq 'observation.recovery') {
                throw 'SAVED_RECOVERY_EVENT_CHANGED_WINDOW'
            }
        }
    } finally {$reader.Dispose()}
    $sample = $life.samples.$Stage
    if (-not $sample) { throw "MEMORY_BOUNDARY_MISSING:$Stage" }
    if ($sample.process_id -ne $target.pid -or $sample.process_created_100ns.ToString() -ne $target.process_start_filetime) {
        throw 'MEMORY_BOUNDARY_PROCESS_IDENTITY_CHANGED'
    }
    if ($sample.utc_ms -gt [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) { throw 'MEMORY_BOUNDARY_TIME_INVALID' }
    return @{run_id=$Run.run_id;generation=$Run.generation;service_instance=$target.instance_id;
        run_directory=$directory;stage=$Stage;sample=$sample;
        batch_request_id=$Run.repeat.request_id;completed_cycles=$Run.repeat.completed_cycles}
}
function Stop-OwnTask {
    if (-not $requestId) { return }
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Check-Identity
        $run = Current-Run
        if (-not $run.busy -and $run.quiescent) { return }
        if (-not (($run.repeat -and $run.repeat.request_id -eq $requestId) -or
            ($run.submission -and $run.submission.request_id -eq $requestId))) { throw 'OWN_TASK_STATUS_CHANGED' }
        # A handoff submission can have another id under our repeat request.
        # The server checks the current submission id atomically before stopping.
        if (-not $run.submission -or -not $run.submission.request_id) { throw 'OWN_SUBMISSION_ID_MISSING' }
        try {
            Invoke-RestMethod -Method Post "$url/api/v1/runs/current/stop" -ContentType 'application/json' -Body (
                @{request_id=$run.submission.request_id} | ConvertTo-Json -Compress) -TimeoutSec 5 | Out-Null
        } catch { if (($_.ToString() + [string]$_.ErrorDetails) -notmatch 'SUBMISSION_ID_MISMATCH') { throw } }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'OWN_TASK_STOP_UNCONFIRMED'
}
try {
    $target = Invoke-RestMethod "$url/api/v1/service" -TimeoutSec 5
    if ($target.executable -ne (Join-Path $candidate 'automationd.exe') -or $target.data_root -ne $data) {
        throw 'SERVICE_CANDIDATE_OR_DATA_MISMATCH'
    }
    Check-Identity
    $run = Current-Run
    if ($Mode -eq 'Monitor') {
        if (-not $run.repeat -or -not $run.repeat.active -or $run.repeat.request_id -ne $BatchRequestId) {
            throw 'EXPECTED_ACTIVE_BATCH_MISSING'
        }
        $result.batch_request_id=$BatchRequestId
        $result.batch_completed_at_attach=[int]$run.repeat.completed_cycles
    } elseif ($run.busy -or -not $run.quiescent) { throw 'SERVICE_MUST_BE_IDLE' }
    $a = [WvdTraceNative]::Index($target.executable)
    $b = [WvdTraceNative]::Index([IO.Path]::GetFullPath($PdbPath))
    if ($a.Guid -eq [Guid]::Empty -or $a.Guid -ne $b.Guid -or $a.Age -ne $b.Age) { throw 'EXE_PDB_MISMATCH' }
    $identity = @{exe_sha256=(Get-FileHash -LiteralPath $target.executable).Hash;
        pdb_sha256=(Get-FileHash -LiteralPath $PdbPath).Hash;pdb_path=[IO.Path]::GetFullPath($PdbPath);
        guid=$a.Guid.ToString();age=$a.Age;target=$target}
    $profilePath = Join-Path $root 'capture.wprp'
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'profiles/memory-stacks.wprp') -Destination $profilePath
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'profiles/memory-stacks.wpaProfile') -Destination (Join-Path $root 'analysis.wpaProfile')
    if ($CaptureKind -eq 'HeapSnapshots') {
        $capture=[xml](Get-Content -Encoding utf8 -Raw -LiteralPath $profilePath)
        foreach ($node in @($capture.SelectNodes('//Keyword[@Value="VirtualAllocation"] | //Stack[@Value="VirtualAllocation"]'))) {
            [void]$node.ParentNode.RemoveChild($node)
        }
        foreach ($node in @($capture.SelectNodes('//Stacks[not(*)]'))) { [void]$node.ParentNode.RemoveChild($node) }
        $capture.Save($profilePath)
        $exportPath=Join-Path $root 'analysis.wpaProfile'
        $export=[xml](Get-Content -Encoding utf8 -Raw -LiteralPath $exportPath)
        $ns=[Xml.XmlNamespaceManager]::new($export.NameTable)
        $ns.AddNamespace('p',$export.DocumentElement.NamespaceURI)
        $node=$export.SelectSingleNode('//p:View[@Title="VirtualAllocCommitLifeTimes"]',$ns)
        [void]$node.ParentNode.RemoveChild($node)
        $export.Save($exportPath)
    }
    $identity.capture_profile_sha256=(Get-FileHash -LiteralPath $profilePath).Hash
    $identity.export_profile_sha256=(Get-FileHash -LiteralPath (Join-Path $root 'analysis.wpaProfile')).Hash
    $profile = $profilePath + '!WvdMemoryStacks'
    Wpr @('-profiledetails', $profile, '-filemode') 'profile-details' | Out-Null
    if ($Mode -ne 'Preflight') {
        $mutex=[Threading.Mutex]::new($false, "Local\WvdMemoryTrace_$($target.pid)_$($target.process_start_filetime)")
        $lockOwned=$mutex.WaitOne(0)
        if (-not $lockOwned) { throw 'TARGET_TRACE_HELPER_ALREADY_ACTIVE' }
    }
    $status = Wpr @('-status') 'status-before'
    if ($status -notmatch 'WPR is not recording') { throw 'EXISTING_TRACE_NOT_OWNED' }
    $named=@([WvdTraceNative]::Discover('') | Where-Object {$_.Name -like 'WPR_initiated_*'})
    Write-Json 'named-sessions-before.json' ($named | Select-Object Name,File)
    if ($named.Count) { throw 'EXISTING_NAMED_TRACE_NOT_OWNED' }
    Assert-NoExistingWprCapture
    $config = Wpr @('-snapshotconfig', 'heap', '-pid', [string]$target.pid) 'snapshot-config-before'
    if ($config -notmatch 'snapshot is disabled') { throw 'EXISTING_SNAPSHOT_CONFIG_NOT_OWNED' }
    Write-Json 'identity.json' $identity
    Write-Json 'plan.json' $result
    if ($Mode -eq 'Preflight') { $result.complete=$true; return }
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $admin) { throw 'CAPTURE_REQUIRES_ADMINISTRATOR' }
    $AnalyzerPath=[IO.Path]::GetFullPath($AnalyzerPath)
    if (-not (Test-Path -LiteralPath $AnalyzerPath -PathType Leaf)) { throw 'FINAL_TRACE_ANALYZER_MISSING' }
    $result.analyzer_sha256=(Get-FileHash -LiteralPath $AnalyzerPath).Hash
    $template = $null
    if ($Mode -eq 'Collect') {
        $template = Get-Content -Encoding utf8 -Raw -LiteralPath $TaskRequestPath | ConvertFrom-Json -AsHashtable
        if ($template.task_id -ne 'GiantBounty' -or $template.resource_locale -ne 'zh-Hant') { throw 'EXPECTED_GIANT_ZH_HANT_REQUEST' }
        if ($EstimatedRoundSeconds -le 0 -or 2*$EstimatedRoundSeconds+180+3*33 -gt
            ($absoluteDeadline-[DateTime]::UtcNow).TotalSeconds) { throw 'TWO_ROUND_PREPARATION_EXECUTION_CLEANUP_BUDGET_NOT_PROVEN' }
    }
    $profileFile = Join-Path $data 'profile.json'
    $document = Get-Content -Encoding utf8 -Raw -LiteralPath $profileFile | ConvertFrom-Json
    if ($Mode -in @('Collect','Monitor') -and $document.logging -and
        (-not $document.logging.memory -or $document.logging.level -notin @('trace', 'debug', 'info'))) {
        throw 'WORKER_BOUNDARIES_REQUIRE_EXISTING_MEMORY_LOGGING'
    }
    $profileHash = (Get-FileHash -LiteralPath $profileFile).Hash
    $temp = Join-Path $root 'temp'
    New-Item -ItemType Directory -Path $temp | Out-Null
    $snapshotAttempted = $true
    Write-Json 'ownership-intent.json' @{session=$session;target=$target;snapshot_attempted=$true;trace_attempted=$false;
        prior_snapshot_state='disabled';prior_named_sessions=0;observed_utc=[DateTime]::UtcNow.ToString('o')}
    Wpr @('-snapshotconfig', 'heap', '-pid', [string]$target.pid, 'enable') 'snapshot-enable' | Out-Null
    $snapshotOwned = $true
    $traceAttempted = $true
    Write-Json 'ownership-intent.json' @{session=$session;target=$target;snapshot_attempted=$true;trace_attempted=$true;
        prior_snapshot_state='disabled';prior_named_sessions=0;observed_utc=[DateTime]::UtcNow.ToString('o')}
    Wpr @('-start', $profile, '-filemode', '-recordtempto', $temp, '-instancename', $session) 'start' | Out-Null
    $traceOwned = $true
    $names = @([WvdTraceNative]::Discover($session) | ForEach-Object { $_.Name })
    Snapshot $(if ($Mode -eq 'Monitor') {'attached_partial_window'} else {'before_tasks'})
    if ($Mode -eq 'Probe') {
        Start-Sleep -Seconds 2
        if ((Current-Run).busy) { throw 'PROBE_INTERRUPTED_BY_USER_TASK' }
        Snapshot 'idle_probe'
    } elseif ($Mode -eq 'Monitor') {
        # Monitor never owns the batch. An explicitly requested partial first
        # round is labeled separately; all joins require repeat.waiting.
        $baselineCount=$null
        $lastJoinedRun=0
        $observedRuns=[Collections.Generic.HashSet[long]]::new()
        while ($completed -lt 2) {
            Check-Health
            $run=Current-Run
            Write-Json 'current-run.json' ($run | Select-Object state,busy,quiescent,run_id,run_directory,repeat,submission)
            if (-not $run.repeat -or $run.repeat.request_id -ne $BatchRequestId) { throw 'MONITORED_BATCH_CHANGED' }
            if ($run.state -eq 'Recovering') { throw 'DEVICE_OR_APPLICATION_RECOVERY_CHANGED_WINDOW' }
            if (-not $run.repeat.active -and $run.repeat.state -ne 'completed') { throw 'MONITORED_BATCH_STOPPED' }
            $count=[int]$run.repeat.completed_cycles
            if ($null -ne $baselineCount -and $run.run_id -gt $lastJoinedRun -and $run.state -eq 'Running') {
                if ($observedRuns.Add([long]$run.run_id)) { $result.rounds_started++ }
            }
            if ($run.state -eq 'Completed' -and $run.quiescent -and $run.repeat.active -and $run.repeat.state -eq 'waiting' -and
                $run.run_id -ne $lastJoinedRun) {
                $boundary=Joined-Boundary $run
                if ($null -eq $baselineCount) {
                    if ($MonitorCurrentRound) {
                        $phase='worker_joined_1_partial'; $baselineCount=$count-1
                    } else { $phase='joined_baseline'; $baselineCount=$count }
                    $result.baseline_completed_cycles=$baselineCount
                } else {
                    if ($count -ne $baselineCount+$completed+1) { throw 'MONITORED_JOIN_WINDOW_MISSED' }
                    $phase='worker_joined_'+($completed+1)
                }
                $boundary.completed_cycles=$count
                Snapshot $phase $boundary
                $after=Current-Run
                Assert-TraceSnapshotBoundary $run $after 'worker_joined'
                $lastJoinedRun=[long]$run.run_id
                if ($phase -ne 'joined_baseline') {
                    $completed++; $result.rounds_completed=$completed
                    if ($observedRuns.Add([long]$run.run_id)) { $result.rounds_started++ }
                }
            }
            if (-not $run.repeat.active -and $completed -lt 2) { throw 'BATCH_ENDED_BEFORE_TWO_FULL_WINDOWS' }
            Start-Sleep -Milliseconds 250
        }
        $result.monitored_run_ids=@($observedRuns | Sort-Object)
        $result.game_batch_not_stopped=$true
    } else {
        # One-round submissions provide a real join barrier before the next run.
        for ($round=1; $round -le 2; ++$round) {
            Check-Health
            if ($EstimatedRoundSeconds+33+180 -gt ($absoluteDeadline-[DateTime]::UtcNow).TotalSeconds) {
                throw 'NEXT_ROUND_AND_CLEANUP_DO_NOT_FIT_DEADLINE'
            }
            if ((Current-Run).busy) { throw 'SERVICE_BUSY_BEFORE_SUBMISSION' }
            if ((Get-FileHash -LiteralPath $profileFile).Hash -ne $profileHash) { throw 'PROFILE_CHANGED' }
            $requestId = [Guid]::NewGuid().ToString()
            $body = @{} + $template
            $body.request_id=$requestId; $body.repeat=$true; $body.repeat_count=1
            Write-Json "request-$round.json" $body
            Invoke-RestMethod -Method Post "$url/api/v1/runs/start" -ContentType 'application/json' -Body (
                $body | ConvertTo-Json -Depth 16 -Compress) -TimeoutSec 5 | Out-Null
            $result.rounds_submitted++
            $roundStarted=$false
            while ($true) {
                Check-Health
                $run = Current-Run
                Write-Json 'current-run.json' ($run | Select-Object state,busy,quiescent,run_id,run_directory,repeat,submission)
                if ($run.submission -and $run.submission.request_id -eq $requestId -and $run.submission.state -in @('failed', 'cancelled')) {
                    throw "PREPARATION_FAILED:$($run.submission.error)"
                }
                if ($run.repeat -and $run.repeat.request_id -eq $requestId) {
                    if (-not $roundStarted -and $run.run_id -gt 0 -and $run.state -in @('Running','Recovering','Completed','Failed','UserStopped')) {
                        $result.rounds_started++; $roundStarted=$true
                    }
                    if ($run.state -eq 'Recovering') { throw 'DEVICE_OR_APPLICATION_RECOVERY_CHANGED_WINDOW' }
                    if (-not $run.repeat.active -and -not $run.busy -and $run.quiescent) { break }
                }
                Start-Sleep -Seconds 1
            }
            if ($run.state -ne 'Completed' -or $run.repeat.state -ne 'completed' -or $run.repeat.completed_cycles -ne 1) {
                throw 'ROUND_NOT_COMPLETED_CLEANLY'
            }
            Snapshot "batch_payloads_released_$round" (Joined-Boundary $run 'batch_payloads_released')
            $completed++; $result.rounds_completed=$completed; $requestId=''
        }
    }
    Check-Health
    Write-Json 'collectors-before-stop.json' @($names | ForEach-Object { [WvdTraceNative]::Query($_) })
    $stopAttempted=$true
    $stop = Wpr @('-stop', (Join-Path $root 'allocations.etl'), '-skipPdbGen', '-compress', '-instancename', $session) 'stop' 120
    $traceOwned = $false
    $bytes = Get-TraceFileBytes $root
    if ($bytes+$PriorTraceBytes -gt 512MB) { throw 'TOTAL_TRACE_BUDGET_EXCEEDED' }
    $result.trace_bytes=$bytes; $result.complete=$true
} catch {
    $failure = $_.Exception.Message
} finally {
    try {
        if ($failure) { try { Stop-OwnTask } catch { $failure += ';' + $_.Exception.Message } }
        $cleanup=Close-OwnedTraceCapture @{trace_attempted=$traceAttempted;snapshot_attempted=$snapshotAttempted;
            stop_attempted=$stopAttempted;session=$session;root=$root;target=$target} {
                param($arguments,$name,$seconds) Wpr $arguments $name $seconds
            }
        $result.cleanup=$cleanup; $result.cleanup_confirmed=$cleanup.cleanup_confirmed
        foreach($error in $cleanup.errors) { $failure+=';'+$error }
        $result.elapsed_seconds=$watch.Elapsed.TotalSeconds
        $result.side_effects_attempted=@{snapshot=$snapshotAttempted;trace=$traceAttempted}
        $result.observer_protocol=@{quiet_seconds=1;normal_current_poll_seconds=5;requests=@($observerRequests);
            other_observers='unknown; existing UI may still poll';http_callbacks_proven_destroyed=$false}
        $result.trace_bytes=Get-TraceFileBytes $root
        $result.aggregate_trace_bytes=$result.trace_bytes+$PriorTraceBytes
        if ($result.aggregate_trace_bytes -gt 512MB) { $failure+=';TOTAL_TRACE_BUDGET_EXCEEDED' }
        if ([DateTime]::UtcNow -gt $absoluteDeadline) { $failure+=';CAPTURE_DEADLINE_EXCEEDED' }
        # Persist cleanup before invoking even the lightweight SDK file check.
        Write-Json 'capture-cleanup.json' $result
        $final=if(Test-Path (Join-Path $root 'allocations.etl')){Join-Path $root 'allocations.etl'}elseif(Test-Path (Join-Path $root 'incomplete.etl')){Join-Path $root 'incomplete.etl'}else{$null}
        if ($final -and $cleanup.cleanup_confirmed) {
            try {
                $integrity=Read-TraceFileIntegrity $AnalyzerPath $final $root
                Write-Json 'final-trace-integrity.json' $integrity
                $result.events_lost=$integrity.events_lost; $result.buffers_lost=$integrity.buffers_lost
                $result.final_etl_sha256=(Get-FileHash -LiteralPath $final).Hash
                Assert-TraceFileIntegrity $integrity
                $result.final_trace_validated=$true
            } catch { $failure+=';'+$_.Exception.Message }
        }
        if ($Mode -ne 'Preflight' -and $result.complete -and -not $result.final_trace_validated) { $failure+=';FINAL_TRACE_NOT_VALIDATED' }
        if ($failure) { $result.complete=$false }
        $result.capture_complete=($Mode -ne 'Preflight' -and $result.complete)
        $result.failure=$failure
        Write-Json 'collector-health.json' $health
        Write-Json 'receipt.json' $result
    } finally {
        try { if ($lockOwned) { $mutex.ReleaseMutex() } }
        finally { if ($mutex) { $mutex.Dispose() } }
    }
}
if ($failure) { throw $failure }
Write-Output "Memory trace $Mode finished: $root"

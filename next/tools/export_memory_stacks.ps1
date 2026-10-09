#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$EvidenceRoot,[Parameter(Mandatory)][string]$PdbPath,
      [ValidateSet('analyze','inspect-incomplete')][string]$Mode='analyze',
      [string]$AnalyzerPath=(Join-Path $PSScriptRoot 'heap_analyzer/bin/Release/net8.0/HeapAnalyzer.exe'),
      [ValidatePattern('^[a-zA-Z0-9-]{1,64}$')][string]$OutputName='analysis-direct',
      [ValidateRange(128,4096)][int]$MemoryMiB=1024)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'memory_trace_support.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'measurement_endpoint.psm1') -Force
Initialize-TraceInterop
$root=[IO.Path]::GetFullPath($EvidenceRoot)
$identity=Read-TraceJson (Join-Path $root 'identity.json')
$receipt=Read-TraceJson (Join-Path $root 'receipt.json')
$completionPath=Join-Path $root 'receipt-completion.json'
$completion=if(Test-Path -LiteralPath $completionPath){Read-TraceJson $completionPath}else{$null}
if($Mode -eq 'analyze' -and (-not $completion -or -not $completion.complete -or -not $completion.primary_receipt_deadline_met)) {
    throw 'CAPTURE_PRIMARY_RECEIPT_DEADLINE_NOT_MET'
}
$data=[IO.Path]::GetFullPath($identity.target.data_root).TrimEnd('\','/')
if($root -eq $data -or $root.StartsWith($data+[IO.Path]::DirectorySeparatorChar,'OrdinalIgnoreCase')){throw 'ANALYSIS_MUST_NOT_WRITE_AUTHORITY_DATA'}
if(-not $receipt.cleanup_confirmed){throw 'CAPTURE_CLEANUP_NOT_CONFIRMED'}
if($Mode -eq 'analyze' -and (-not $receipt.complete -or
    -not $receipt.PSObject.Properties['final_trace_validated'] -or -not $receipt.final_trace_validated)) {
    throw 'INCOMPLETE_CAPTURE_REQUIRES_EXPLICIT_INSPECT_MODE'
}
$etl=if(Test-Path (Join-Path $root 'allocations.etl')){Join-Path $root 'allocations.etl'}elseif(Test-Path (Join-Path $root 'incomplete.etl')){Join-Path $root 'incomplete.etl'}else{throw 'ETL_MISSING'}
$etlHash=(Get-FileHash -LiteralPath $etl).Hash
if($receipt.PSObject.Properties['final_etl_sha256'] -and $receipt.final_etl_sha256 -ne $etlHash){throw 'FINAL_ETL_HASH_CHANGED'}
if((Get-FileHash -LiteralPath $identity.target.executable).Hash -ne $identity.exe_sha256){throw 'SOURCE_EXE_CHANGED'}
$pdb=[IO.Path]::GetFullPath($PdbPath)
$index=[WvdTraceNative]::Index($pdb)
if($index.Guid.ToString() -ne $identity.guid -or $index.Age -ne $identity.age -or
    (Get-FileHash -LiteralPath $pdb).Hash -ne $identity.pdb_sha256){throw 'CAPTURE_PDB_MISMATCH'}
if((Get-FileHash -LiteralPath (Join-Path $root 'capture.wprp')).Hash -ne $identity.capture_profile_sha256){throw 'FROZEN_CAPTURE_PROFILE_CHANGED'}
$analyzer=[IO.Path]::GetFullPath($AnalyzerPath)
$dll=Join-Path ([IO.Path]::GetDirectoryName($analyzer)) 'HeapAnalyzer.dll'
if(-not (Test-Path -LiteralPath $analyzer) -or -not (Test-Path -LiteralPath $dll)){throw 'BUILD_HEAP_ANALYZER_FIRST'}
$out=Join-Path $root $OutputName
if(Test-Path $out){throw 'ANALYSIS_OUTPUT_ALREADY_EXISTS'}
$plan=[ordered]@{capture_complete=[bool]$receipt.complete;analysis_complete=$false;comparison_eligible=$false;
    attribution_complete=$false;mode=$Mode;input_etl_sha256=$etlHash;process_start_filetime=$identity.target.process_start_filetime;
    target_pid=$identity.target.pid;analyzer_exe_sha256=(Get-FileHash $analyzer).Hash;
    analyzer_dll_sha256=(Get-FileHash $dll).Hash;package_version='1.12.10';max_private_mib=$MemoryMiB;
    max_seconds=120;max_output_bytes=128MB;max_system_commit_ratio=0.98;virtual_alloc_analyzed=$false;failure=''}
$plan | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $root ($OutputName+'-plan.json'))
try {
    Invoke-TraceTool -Executable $analyzer -Arguments @('analyze','--etl',$etl,'--pid',[string]$identity.target.pid,
        '--target-filetime',$identity.target.process_start_filetime,'--target-image',[IO.Path]::GetFileName($identity.target.executable),
        '--output',$out,'--pdb-directory',[IO.Path]::GetDirectoryName($pdb),'--memory-mib',[string]$MemoryMiB,
        '--output-mib','128','--capture-receipt',(Join-Path $root 'receipt.json'),'--checkpoints',(Join-Path $root 'checkpoints.json')) `
        -Log (Join-Path $root ($OutputName+'.log')) -TimeoutSeconds 120 -OutputRoot $out `
        -MaxPrivateBytes ($MemoryMiB*1MB) -MaxSystemCommitRatio 0.98 | Out-Null
    if((Get-FileHash -LiteralPath $etl).Hash -ne $etlHash){throw 'ETL_CHANGED_DURING_ANALYSIS'}
    $result=Read-TraceJson (Join-Path $out 'analysis-receipt.json')
    if(-not $result.analysis_complete -or $result.package_version -ne '1.12.10'){throw 'ANALYZER_RESULT_NOT_COMPLETE'}
    $snapshots=Read-TraceJson (Join-Path $out 'snapshot-map.json')
    $checkpoints=Read-TraceJson (Join-Path $root 'checkpoints.json')
    if($snapshots.Count -gt 256 -or $checkpoints.Count -gt 256){throw 'TRACE_CHECKPOINT_RECORD_BUDGET_EXCEEDED'}
    $used=[Collections.Generic.HashSet[string]]::new()
    foreach($snapshot in $snapshots){
        $time=[DateTimeOffset]$snapshot.utc
        $matches=@($checkpoints | Where-Object {$time -ge [DateTimeOffset]$_.began_utc -and $time -le [DateTimeOffset]$_.ended_utc})
        if($matches.Count -ne 1){throw 'SNAPSHOT_TIMESTAMP_NOT_UNIQUELY_BOUND'}
        $checkpoint=$matches[0]
        if($checkpoint.pid -ne $identity.target.pid -or $checkpoint.process_start_filetime -ne $identity.target.process_start_filetime){throw 'CHECKPOINT_PROCESS_IDENTITY_CHANGED'}
        if(-not $used.Add($snapshot.process_instance+'/'+$snapshot.snapshot_id)){throw 'DUPLICATE_PROCESS_SNAPSHOT'}
        $valid=$false
        if($checkpoint.boundary -and $checkpoint.boundary.PSObject.Properties['stage'] -and
            $checkpoint.boundary.PSObject.Properties['before'] -and $checkpoint.boundary.PSObject.Properties['after']) {
            Assert-TraceSnapshotBoundary $checkpoint.boundary.before $checkpoint.boundary.after $checkpoint.boundary.stage
            $valid=$snapshot.creation_verified
            if($checkpoint.phase -match '^worker_joined_[12]$') {
                Assert-MeasurementEndpoint $checkpoint.boundary.measurement $identity.target
                Assert-MeasurementEndpoint $checkpoint.boundary.measurement_after $identity.target
                Assert-MeasurementEndpoint $checkpoint.boundary.measurement_ack $identity.target
                if(-not $checkpoint.boundary.measurement_ack.acknowledged -or
                    $checkpoint.boundary.measurement.sequence -ne $checkpoint.boundary.measurement_after.sequence -or
                    $checkpoint.boundary.measurement.sequence -ne $checkpoint.boundary.measurement_ack.sequence) {
                    throw 'MEASUREMENT_CAPTURE_NOT_HELD_AND_ACKNOWLEDGED'
                }
                $snapshot|Add-Member -NotePropertyName measurement -NotePropertyValue $checkpoint.boundary.measurement
            }
            if($checkpoint.boundary.PSObject.Properties['os_before'] -and $checkpoint.boundary.PSObject.Properties['os_after']) {
                $a=$checkpoint.boundary.os_before; $b=$checkpoint.boundary.os_after
                $valid=($valid -and $a.pid -eq $identity.target.pid -and $b.pid -eq $identity.target.pid -and
                    $a.process_start_filetime -eq $identity.target.process_start_filetime -and
                    $b.process_start_filetime -eq $identity.target.process_start_filetime)
            }
        }
        $snapshot | Add-Member -NotePropertyName boundary_verified -NotePropertyValue $valid
    }
    $snapshots | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $out 'snapshot-map.json')
    $plan.analysis_complete=$true
    $plan.peak_private_bytes=$result.peak_private_commit_bytes
    $plan.elapsed_seconds=$result.elapsed_seconds
    $plan.output_bytes=Get-TraceFileBytes -Root $out -Filter '*'
    $plan.snapshot_totals_conserved=$true;$plan.delta_totals_conserved=$true
    # The early background snapshot is not the main comparison endpoint.
    $joined=@($snapshots | Where-Object {$_.boundary_verified -and $_.phase -match '^worker_joined_[12]$'})
    $released=@($snapshots | Where-Object {$_.boundary_verified -and $_.phase -match '^batch_payloads_released_[12]$'})
    if($joined.Count -gt 0 -and $released.Count -gt 0){throw 'MIXED_MEASUREMENT_RELEASE_SCOPES'}
    $endpoints=if($joined.Count){$joined}else{$released}
    if($joined.Count -eq 2){Assert-MeasurementPair $joined[0].measurement $joined[1].measurement $identity.target}
    $plan.comparison_phase=if($joined.Count){'worker_joined'}else{'batch_payloads_released'}
    $plan.comparison_eligible=($Mode -eq 'analyze' -and $receipt.complete -and $endpoints.Count -eq 2 -and
        $endpoints[0].process_instance -eq $endpoints[1].process_instance -and $endpoints[0].is_32_bit -eq $endpoints[1].is_32_bit)
} catch {$plan.failure=$_.Exception.Message}
finally {$plan | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $root ($OutputName+'-receipt.json'))}
if($plan.failure){throw $plan.failure}
Write-Output (Join-Path $root ($OutputName+'-receipt.json'))

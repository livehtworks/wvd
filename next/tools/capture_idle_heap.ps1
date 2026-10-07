#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EvidenceRoot,
    [Parameter(Mandatory)][string]$Cdb,
    [Parameter(Mandatory)][string]$Pdb,
    [Parameter(Mandatory)][string]$SymbolCache,
    [int]$Port=17654,
    [switch]$AllocationSizes
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'memory_trace_support.psm1') -Force
Initialize-TraceInterop
if(-not ('WvdMemoryRegions' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'memory_region_layout.cs') }
$root=[IO.Path]::GetFullPath($EvidenceRoot)
if(Test-Path -LiteralPath $root){throw 'FRESH_HEAP_LAYOUT_EVIDENCE_REQUIRED'}
$url="http://127.0.0.1:$Port"
function State {
    $service=Invoke-RestMethod "$url/api/v1/service" -TimeoutSec 3
    $run=Invoke-RestMethod "$url/api/v1/runs/current" -TimeoutSec 3
    $device=Invoke-RestMethod "$url/api/v1/device" -TimeoutSec 3
    if($run.busy -or -not $run.quiescent -or $device.operation.state -eq 'running'){
        throw 'HEAP_LAYOUT_REQUIRES_QUIESCENT_SERVICE'
    }
    $process=Get-Process -Id $service.pid -ErrorAction Stop
    if($process.StartTime.ToFileTimeUtc().ToString() -ne $service.process_start_filetime){throw 'HEAP_LAYOUT_PID_REUSED'}
    $boundary=$null
    if($run.run_id -gt 0){
        $memory=Get-Content -LiteralPath (Join-Path $run.run_directory 'memory-lifecycle.json') -Raw -Encoding utf8 | ConvertFrom-Json
        $boundary=$memory.samples.batch_payloads_released
        if(-not $boundary -or $boundary.process_id -ne $service.pid -or
            [string]$boundary.process_created_100ns -ne $service.process_start_filetime){throw 'HEAP_LAYOUT_RELEASE_BOUNDARY_MISSING'}
    }
    return @{service=$service;run_id=$run.run_id;state=$run.state;run_directory=$run.run_directory;
        boundary=$boundary;utc=[DateTime]::UtcNow.ToString('o');private_bytes=$process.PrivateMemorySize64;
        working_set_bytes=$process.WorkingSet64;threads=$process.Threads.Count;handles=$process.HandleCount}
}
$before=State
$data=[IO.Path]::GetFullPath($before.service.data_root).TrimEnd('\','/')
if($root -eq $data -or $root.StartsWith($data+[IO.Path]::DirectorySeparatorChar,'OrdinalIgnoreCase')){
    throw 'HEAP_LAYOUT_EVIDENCE_INSIDE_AUTHORITY_DATA'
}
$exeIndex=[WvdTraceNative]::Index($before.service.executable)
$pdbPath=[IO.Path]::GetFullPath($Pdb)
$pdbIndex=[WvdTraceNative]::Index($pdbPath)
if($exeIndex.Guid -eq [guid]::Empty -or $exeIndex.Guid -ne $pdbIndex.Guid -or $exeIndex.Age -ne $pdbIndex.Age){throw 'HEAP_LAYOUT_EXE_PDB_MISMATCH'}
[IO.Directory]::CreateDirectory($root) | Out-Null
$receipt=@{complete=$false;method='CDB noninvasive nonsuspending heap summary; quiescent application, not an atomic process snapshot';
    address_classification='VirtualQueryEx region types; not allocator ownership or PrivateUsage';
    before=$before;exe_sha256=(Get-FileHash -LiteralPath $before.service.executable).Hash;
    pdb_sha256=(Get-FileHash -LiteralPath $pdbPath).Hash;max_seconds=30;symbols_guid=$exeIndex.Guid.ToString();symbols_age=$exeIndex.Age}
$watch=[Diagnostics.Stopwatch]::StartNew()
try {
    $regions=[WvdMemoryRegions]::Read([int]$before.service.pid,[uint64]$before.service.process_start_filetime,5000)
    $regions | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'regions.json') -Encoding utf8
    $receipt.regions=@{complete=$true;elapsed_ms=$regions.ElapsedMilliseconds;examined=$regions.Examined;
        private_committed=$regions.PrivateCommitted;mapped_committed=$regions.MappedCommitted;
        image_committed=$regions.ImageCommitted;other_committed=$regions.OtherCommitted}
    $symbols=(Split-Path -Parent $pdbPath)+';srv*'+[IO.Path]::GetFullPath($SymbolCache)+'*https://msdl.microsoft.com/download/symbols'
    # -pvr does not suspend the target, including if the bounded helper is killed.
    # !address may spend a long time loading symbols/classifying regions; keep
    # it out of this short live-service observation.
    $command='.echo WVD_LAYOUT_BEGIN;!heap -s;.echo WVD_LAYOUT_END;qd'
    $receipt.allocation_sizes_requested=[bool]$AllocationSizes
    $log=Join-Path $root 'cdb.log'
    $null=Invoke-TraceTool -Executable ([IO.Path]::GetFullPath($Cdb)) -Arguments @('-pvr','-p',[string]$before.service.pid,'-y',$symbols,'-c',$command) `
        -Log $log -TimeoutSeconds 30 -OutputRoot $root -OutputLimit 16MB -DeadlineUtc ([DateTime]::UtcNow.AddSeconds(30-$watch.Elapsed.TotalSeconds))
    $text=Get-Content -LiteralPath $log -Raw -Encoding utf8
    if($text -match 'Unable to examine|No symbols for|ReadMemory error|Cannot continue|Syntax error' -or
        $text -notmatch '(?m)^WVD_LAYOUT_END\s*$' -or $text -notmatch 'NT HEAP STATS|SEGMENT HEAP STATS'){
        throw 'HEAP_LAYOUT_DEBUGGER_OUTPUT_INCOMPLETE'
    }
    if($AllocationSizes){
        # This debugger prints only heap summaries for -h 0, despite accepting it.
        # Request the concrete process heap and validate the allocation table.
        $heap=[regex]::Match($text,'(?im)^([0-9a-f]{16})\s+00000002\s+')
        if(-not $heap.Success){throw 'PROCESS_HEAP_ADDRESS_MISSING'}
        $statsLog=Join-Path $root 'allocation-sizes.log'
        $command='.echo WVD_SIZES_BEGIN;!heap -stat -h '+$heap.Groups[1].Value+' -grp S 20;.echo WVD_SIZES_END;qd'
        $null=Invoke-TraceTool -Executable ([IO.Path]::GetFullPath($Cdb)) -Arguments @('-pvr','-p',[string]$before.service.pid,'-y',$symbols,'-c',$command) `
            -Log $statsLog -TimeoutSeconds 30 -OutputRoot $root -OutputLimit 16MB -DeadlineUtc ([DateTime]::UtcNow.AddSeconds(30-$watch.Elapsed.TotalSeconds))
        $stats=Get-Content -LiteralPath $statsLog -Raw -Encoding utf8
        $receipt.allocation_sizes_complete=$false
        if($stats -match 'Unable to examine|No symbols for|ReadMemory error|Cannot continue|Syntax error' -or
            $stats -notmatch '(?m)^WVD_SIZES_END\s*$' -or $stats -notmatch '(?i)allocations|busy blocks|total.*size'){
            throw 'HEAP_ALLOCATION_SIZE_TABLE_MISSING'
        }
        $receipt.allocation_sizes_complete=$true
    }
    $after=State
    if($after.service.instance_id -ne $before.service.instance_id -or $after.run_id -ne $before.run_id -or
        $after.service.process_start_filetime -ne $before.service.process_start_filetime -or $after.state -ne $before.state){
        throw 'HEAP_LAYOUT_BOUNDARY_CHANGED'
    }
    $receipt.after=$after
    $receipt.complete=$true
} catch {$receipt.failure=$_.Exception.Message} finally {
    $receipt.elapsed_seconds=$watch.Elapsed.TotalSeconds
    $receipt | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $root 'receipt.json') -Encoding utf8
}
if(-not $receipt.complete){throw $receipt.failure}
Write-Output (Join-Path $root 'receipt.json')

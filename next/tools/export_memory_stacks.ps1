#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$EvidenceRoot,
      [Parameter(Mandatory)][string]$PdbPath,
      [string]$WptRoot='C:/Program Files (x86)/Windows Kits/10/Windows Performance Toolkit')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'memory_trace_support.psm1') -Force
Initialize-TraceInterop
$root=[IO.Path]::GetFullPath($EvidenceRoot)
$identity=Get-Content -Encoding utf8 -Raw -LiteralPath (Join-Path $root 'identity.json') | ConvertFrom-Json
$receipt=Get-Content -Encoding utf8 -Raw -LiteralPath (Join-Path $root 'receipt.json') | ConvertFrom-Json
if (-not $receipt.complete -or -not $receipt.cleanup_confirmed -or $receipt.events_lost -ne 0) {
    throw 'TRACE_CAPTURE_NOT_COMPLETE_OR_CLEAN'
}
$checkpoints=@(Get-Content -Encoding utf8 -Raw -LiteralPath (Join-Path $root 'checkpoints.json') | ConvertFrom-Json)
if ($checkpoints.Count -lt 2) { throw 'SNAPSHOT_BOUNDARIES_MISSING' }
$pidValue=[int]$identity.target.pid
$pdb=[IO.Path]::GetFullPath($PdbPath)
$index=[WvdTraceNative]::Index($pdb)
if ($index.Guid.ToString() -ne $identity.guid -or $index.Age -ne $identity.age -or
    (Get-FileHash -LiteralPath $pdb).Hash -ne $identity.pdb_sha256) { throw 'CAPTURE_PDB_MISMATCH' }
$data=[IO.Path]::GetFullPath($identity.target.data_root).TrimEnd('\','/')
if ($root.StartsWith($data + [IO.Path]::DirectorySeparatorChar, 'OrdinalIgnoreCase') -or $root -eq $data) {
    throw 'EXPORT_MUST_NOT_WRITE_AUTHORITY_DATA'
}
$out=Join-Path $root 'analysis'
if (Test-Path -LiteralPath $out) { throw 'ANALYSIS_OUTPUT_ALREADY_EXISTS' }
New-Item -ItemType Directory -Path $out | Out-Null
$oldSymbols=$env:_NT_SYMBOL_PATH
$summary=[ordered]@{complete=$false;attribution='UNRESOLVED';mode=$receipt.mode;target_pid=$pidValue;
    application_symbols_verified=$false;missing_module_symbols=@();failure=''}
function Integer([string]$Value) {
    [long]::Parse($Value, [Globalization.NumberStyles]::AllowThousands, [Globalization.CultureInfo]::InvariantCulture)
}
try {
    # Local matching PDB only: do not implicitly download every system/process PDB.
    $env:_NT_SYMBOL_PATH=[IO.Path]::GetDirectoryName($pdb)
    $exporter=Join-Path $WptRoot 'wpaexporter.exe'
    $profile=Join-Path $root 'analysis.wpaProfile'
    if ((Get-FileHash -LiteralPath $profile).Hash -ne $identity.export_profile_sha256) { throw 'FROZEN_EXPORT_PROFILE_CHANGED' }
    $text=Invoke-TraceTool -Executable $exporter -Arguments @('-i',(Join-Path $root 'allocations.etl'),
        '-processor','Event Tracing for Windows','-symbols','-profile',$profile,'-outputfolder',$out) `
        -Log (Join-Path $root 'wpa-export.log') -TimeoutSeconds 120 -OutputRoot $out
    if ($text -match 'Lost events detected|Error exporting profile|Unable to export|No data in table') {
        throw 'WPA_EXPORT_CONTENT_FAILURE'
    }
    $heapFile=Join-Path $out 'Heap_Snapshot_WvdHeapSnapshot.csv'
    $virtualFile=Join-Path $out 'VirtualAlloc_Commit_LifeTimes_WvdVirtualAlloc.csv'
    $virtualWanted=$receipt.virtual_alloc_scope -ne 'not_collected_heap_snapshot_only'
    if (-not (Test-Path -LiteralPath $heapFile) -or ($virtualWanted -and -not (Test-Path -LiteralPath $virtualFile))) {
        throw 'EXPECTED_EXPORT_TABLE_MISSING'
    }
    $heap=@(Import-Csv -Encoding utf8 -LiteralPath $heapFile | Where-Object { [int]$_.PID -eq $pidValue })
    if (-not $heap.Count) { throw 'TARGET_HEAP_ROWS_MISSING' }
    $instances=@($heap | Group-Object Instance | Sort-Object {
        [double]::Parse($_.Group[0].'Snap Time (s)', [Globalization.CultureInfo]::InvariantCulture) })
    if ($instances.Count -ne $checkpoints.Count) { throw 'SNAPSHOT_INSTANCE_COUNT_MISMATCH' }
    $aggregates=[Collections.Generic.List[object]]::new()
    $deltas=[Collections.Generic.List[object]]::new()
    $previous=@{}
    $boundaryRows=[Collections.Generic.List[object]]::new()
    for ($i=0;$i -lt $instances.Count;++$i) {
        $phase=$checkpoints[$i].phase
        $current=@{}
        foreach ($row in $instances[$i].Group) {
            if (-not $row.Stack -or $row.Stack -eq 'n/a' -or $row.Stack -match 'Symbols disabled') {
                throw 'TARGET_HEAP_STACK_MISSING'
            }
            if (-not $current.ContainsKey($row.Stack)) { $current[$row.Stack]=@{blocks=0L;bytes=0L} }
            $current[$row.Stack].blocks+=Integer $row.Count
            $current[$row.Stack].bytes+=Integer $row.'Size (B)'
        }
        $totalBlocks=0L; $totalBytes=0L
        foreach ($stack in $current.Keys) {
            $value=$current[$stack]; $totalBlocks+=$value.blocks; $totalBytes+=$value.bytes
            $aggregates.Add([pscustomobject]@{phase=$phase;instance=$instances[$i].Name;
                blocks=$value.blocks;bytes=$value.bytes;stack=$stack})
        }
        $boundaryRows.Add(@{phase=$phase;instance=$instances[$i].Name;blocks=$totalBlocks;bytes=$totalBytes})
        if ($i -gt 0) {
            $keys=@(@($current.Keys)+@($previous.Keys) | Sort-Object -Unique)
            foreach ($stack in $keys) {
                $before=if($previous.ContainsKey($stack)){$previous[$stack]}else{@{blocks=0L;bytes=0L}}
                $after=if($current.ContainsKey($stack)){$current[$stack]}else{@{blocks=0L;bytes=0L}}
                $deltas.Add([pscustomobject]@{from=$checkpoints[$i-1].phase;to=$phase;
                    block_delta=$after.blocks-$before.blocks;byte_delta=$after.bytes-$before.bytes;stack=$stack})
            }
        }
        $previous=$current
    }
    $aggregates | Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath (Join-Path $out 'heap-by-stack.csv')
    $deltas | Sort-Object byte_delta -Descending | Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath (Join-Path $out 'heap-diff.csv')
    $processName='automationd.exe ('+$pidValue+')'
    $virtual=@()
    if ($virtualWanted) { $virtual=@(Import-Csv -Encoding utf8 -LiteralPath $virtualFile | Where-Object {$_.Process -eq $processName}) }
    if ($virtualWanted -and (-not $virtual.Count -or -not @($virtual | Where-Object {$_.'Commit Stack' -ne 'n/a'}).Count)) {
        throw 'TARGET_VIRTUAL_ALLOC_STACK_MISSING'
    }
    if ($virtualWanted) { $virtual | Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath (Join-Path $out 'virtual-alloc-target.csv') }
    $allStacks=@($heap.Stack)
    if ($virtualWanted) { $allStacks+=@($virtual.'Commit Stack') }
    $summary.virtual_alloc_collected=$virtualWanted
    $summary.application_symbols_verified=@($allStacks | Where-Object {$_ -match 'automationd.exe!wvd::'}).Count -gt 0
    $missing=[Collections.Generic.HashSet[string]]::new()
    foreach ($stack in $allStacks) {
        foreach ($match in [regex]::Matches($stack,'([^/]+)!<PDB not found>')) { [void]$missing.Add($match.Groups[1].Value) }
    }
    $summary.missing_module_symbols=@($missing | Sort-Object)
    $summary.boundaries=@($boundaryRows); $summary.virtual_alloc_rows=$virtual.Count
    $summary.heap_and_virtual_alloc_bytes_are_not_added=$true
    $summary.complete=$true
    if (-not $summary.application_symbols_verified) { throw 'APPLICATION_SYMBOLS_NOT_VERIFIED' }
} catch {
    $summary.complete=$false; $summary.failure=$_.Exception.Message
} finally {
    $env:_NT_SYMBOL_PATH=$oldSymbols
    $summary | ConvertTo-Json -Depth 12 | Set-Content -Encoding utf8 -LiteralPath (Join-Path $root 'analysis-summary.json')
}
if (-not $summary.complete) { throw $summary.failure }
Write-Output "Exported target heap/VirtualAlloc stacks: $out"

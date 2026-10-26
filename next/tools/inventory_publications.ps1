#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$DataRoot,
      [Parameter(Mandatory)][string]$OutputPath,
      [string]$CompletedInventoryPath,
      [ValidateRange(10,600)][int]$MaxSeconds=180)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$data=[IO.Path]::GetFullPath($DataRoot)
$output=[IO.Path]::GetFullPath($OutputPath)
if ($output.StartsWith($data+[IO.Path]::DirectorySeparatorChar,'OrdinalIgnoreCase') -or (Test-Path -LiteralPath $output)) {
    throw 'NEW_NON_AUTHORITY_OUTPUT_REQUIRED'
}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class WvdPublicationDisk {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern uint GetCompressedFileSizeW(string path, out uint high);
    public static ulong Bytes(string path) {
        Marshal.SetLastPInvokeError(0);
        uint high; uint low=GetCompressedFileSizeW(path,out high);
        int error=Marshal.GetLastPInvokeError();
        if(low==uint.MaxValue && error!=0) throw new System.ComponentModel.Win32Exception(error);
        return ((ulong)high<<32)|low;
    }
}
'@
$watch=[Diagnostics.Stopwatch]::StartNew()
$inputBytes=0L;$records=0L
function Assert-Plain([string]$Path) {
    if(([IO.File]::GetAttributes($Path) -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'INVENTORY_REPARSE_NOT_FOLLOWED'}
    if($Path.Length -gt 4096){throw 'INVENTORY_PATH_LIMIT'}
}
function Read-BoundedJson([string]$Path,[long]$Limit=4MB) {
    Assert-Plain $Path
    $file=[IO.File]::Open($Path,'Open','Read','Read')
    try {
        if($file.Length -gt $Limit -or $script:inputBytes+$file.Length -gt 128MB){throw 'INVENTORY_INPUT_BYTES_LIMIT'}
        $script:inputBytes+=$file.Length
        $reader=[IO.StreamReader]::new($file,[Text.UTF8Encoding]::new($false,$true))
        try {return ($reader.ReadToEnd()|ConvertFrom-Json -Depth 32)} finally {$reader.Dispose()}
    } finally {$file.Dispose()}
}
function Check-Record {
    $script:records++
    if($script:records -gt 100000 -or $watch.Elapsed.TotalSeconds -gt $MaxSeconds){throw 'INVENTORY_RECORD_OR_TIME_LIMIT'}
}
Assert-Plain $data
$published=Join-Path $data 'published'
$rows=[Collections.Generic.List[object]]::new()
$errors=[Collections.Generic.List[object]]::new()
$references=@{}
$resumed=@{}
if($CompletedInventoryPath) {
    $previous=Read-BoundedJson $CompletedInventoryPath 32MB
    if($previous.data_root -ne $data -or -not $previous.read_only){throw 'INVENTORY_SOURCE_SCOPE_MISMATCH'}
    foreach($row in $previous.rows) {
        Check-Record
        if($resumed.Count -ge 5000){throw 'INVENTORY_RESUME_ROW_LIMIT'}
        if([string]$row.id -notmatch '^(?:[A-Za-z0-9._-]{1,192}|(?:[.]asset-sets|asset-objects)/[A-Za-z0-9._-]{1,192})$'){throw 'INVENTORY_RESUME_ID_INVALID'}
        if([string]$row.id -match '(?:^|/)\.\.?(?:/|$)'){throw 'INVENTORY_RESUME_ID_INVALID'}
        if($previous.schema_version -ne 2 -or 'entry_type' -notin $row.PSObject.Properties.Name){continue}
        if($row.unknown_entries -eq 0 -and (Test-Path -LiteralPath (Join-Path $published $row.id) )) {
            $resumed[$row.id]=$row
        }
    }
}
$runs=Join-Path $data 'runs'
$referencesComplete=$true
:RunReferences foreach($instance in [IO.Directory]::EnumerateDirectories($runs)) {
    Assert-Plain $instance
    foreach($run in [IO.Directory]::EnumerateDirectories($instance)) {
        if($watch.Elapsed.TotalSeconds -gt $MaxSeconds){$referencesComplete=$false;break RunReferences}
        $path=Join-Path $run 'run.json'
        if(-not (Test-Path -LiteralPath $path -PathType Leaf)){$referencesComplete=$false;continue}
        try {
            Check-Record
            Assert-Plain $run
            $record=Read-BoundedJson $path
            $id=[string]$record.definition.request_id
            if(-not $references.ContainsKey($id)){$references[$id]=0}
            $references[$id]++
        } catch {
            $referencesComplete=$false
            if($errors.Count -lt 256){$errors.Add(@{path=$path;operation='run_reference';error=$_.Exception.Message})}
            if($_.Exception.Message -match 'LIMIT'){break RunReferences}
        }
    }
}
$complete=$referencesComplete
$objects=[Collections.Generic.List[object]]::new()
$requestSets=@{};$setReferences=@{};$setRequestReferences=@{}
function Is-InternalTransaction([string]$Name) {
    return $Name -match '^[A-Za-z0-9_-]{1,128}[.]staging-[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$' -or
        $Name -match '^[.](?:cleanup-pending(?:-owned)?|prepare-pending(?:-owned)?)-[a-f0-9]{64}(?:[.]json)?$'
}
try {
foreach($directory in [IO.Directory]::EnumerateFileSystemEntries($published)) {
    Check-Record
    if($objects.Count -ge 5000){throw 'INVENTORY_OBJECT_LIMIT'}
    Assert-Plain $directory
    $id=[IO.Path]::GetFileName($directory)
    if(-not [IO.Directory]::Exists($directory)) {
        $kind=if(Is-InternalTransaction $id){'transaction'}else{'unknown'}
        $objects.Add(@{id=$id;path=$directory;kind=$kind;file=$true});continue
    }
    if($id -in @('.asset-sets','asset-objects')) {
        $objects.Add(@{id=$id;path=$directory;kind='private_root';file=$false})
        $kind=if($id -eq '.asset-sets'){'resource_set'}else{'model_object'}
        foreach($child in [IO.Directory]::EnumerateFileSystemEntries($directory)) {
            Check-Record
            if($objects.Count -ge 5000){throw 'INVENTORY_OBJECT_LIMIT'}
            Assert-Plain $child
            $childId=[IO.Path]::GetFileName($child)
            if(-not [IO.Directory]::Exists($child)) {
                $childKind=if(Is-InternalTransaction $childId){'transaction'}else{'unknown'}
                $objects.Add(@{id=$id+'/'+$childId;path=$child;kind=$childKind;file=$true});continue
            }
            $childKind=if($childId -match '^[a-f0-9]{64}$'){$kind}elseif(Is-InternalTransaction $childId){'transaction'}else{'unknown'}
            $objects.Add(@{id=$id+'/'+$childId;path=$child;kind=$childKind;file=$false})
        }
        continue
    }
    $kind=if($id -match '^[A-Za-z0-9_-]{1,128}$'){'request'}elseif(Is-InternalTransaction $id){'transaction'}else{'unknown'}
    $candidate=@{id=$id;path=$directory;kind=$kind;file=$false}
    $objects.Add($candidate)
    if($kind -ne 'request'){continue}
    $marker=Join-Path $directory 'publication-layout.json'
    if(Test-Path -LiteralPath $marker) {
        try {
            if(-not (Test-Path -LiteralPath $marker -PathType Leaf)){throw 'INVENTORY_LAYOUT_NOT_FILE'}
            $layout=Read-BoundedJson $marker 64KB
            if($layout.schema -ne 1 -or $layout.kind -ne 'shared_resource_set' -or
               [string]$layout.resource_digest -notmatch '^[a-f0-9]{64}$' -or
               [string]$layout.program_revision -notmatch '^[a-f0-9]{64}$'){throw 'INVENTORY_LAYOUT_UNKNOWN'}
            $digest=[string]$layout.resource_digest
            $requestSets[$id]=$digest
            if(-not $setRequestReferences.ContainsKey($digest)){$setRequestReferences[$digest]=0;$setReferences[$digest]=0}
            $setRequestReferences[$digest]++
            if($references.ContainsKey($id)){$setReferences[$digest]+=$references[$id]}
        } catch {
            $referencesComplete=$false;$complete=$false;$candidate.kind='unknown'
            if($errors.Count -lt 256){$errors.Add(@{path=$marker;operation='request_resource_reference';error=$_.Exception.Message})}
        }
    }
}
} catch {
    $referencesComplete=$false;$complete=$false
    if($errors.Count -lt 256){$errors.Add(@{path=$published;operation='object_discovery';error=$_.Exception.Message})}
}
foreach($item in $objects) {
    if($watch.Elapsed.TotalSeconds -gt $MaxSeconds -or $rows.Count -ge 5000){$complete=$false;break}
    $id=$item.id;$directory=$item.path
    if($resumed.ContainsKey($id)){
        $row=$resumed[$id]
        # Refresh relation fields below; previous byte samples are read-only evidence.
        $logical=$row.logical_bytes;$allocated=$row.allocated_bytes_by_path;$files=$row.files;$unknown=$row.unknown_entries
    } elseif($item.kind -eq 'private_root') {
        $logical=0L;$allocated=0L;$files=0L;$unknown=0L
    } elseif($item.file) {
        Assert-Plain $directory
        $logical=([IO.FileInfo]::new($directory)).Length;$files=1L;$unknown=0L;$allocated=0L
        try {$allocated=[long][WvdPublicationDisk]::Bytes($directory)} catch {$unknown++;if($errors.Count -lt 256){$errors.Add(@{path=$directory;operation='allocated_bytes';error=$_.Exception.Message})}}
    } else {
    $logical=0L;$allocated=0L;$files=0L;$unknown=0L
    $stack=[Collections.Generic.Stack[string]]::new();$stack.Push($directory)
    while($stack.Count) {
        if($watch.Elapsed.TotalSeconds -gt $MaxSeconds){$complete=$false;$unknown++;break}
        $current=$stack.Pop()
        try {
            if(([IO.File]::GetAttributes($current) -band [IO.FileAttributes]::ReparsePoint) -ne 0){
                $unknown++;$errors.Add(@{path=$current;operation='enumeration';error='REPARSE_NOT_FOLLOWED'});continue
            }
            foreach($entry in [IO.Directory]::EnumerateFileSystemEntries($current)) {
                Check-Record
                $attributes=[IO.File]::GetAttributes($entry)
                if(($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
                    $unknown++;if($errors.Count -lt 256){$errors.Add(@{path=$entry;operation='enumeration';error='REPARSE_NOT_FOLLOWED'})};continue
                }
                if(($attributes -band [IO.FileAttributes]::Directory) -ne 0){if($item.kind -ne 'private_root'){$stack.Push($entry)};continue}
                $files++;$logical+=([IO.FileInfo]::new($entry)).Length
                try {$allocated += [long][WvdPublicationDisk]::Bytes($entry)}
                catch {$unknown++;if($errors.Count -lt 256){$errors.Add(@{path=$entry;operation='allocated_bytes';error=$_.Exception.Message})}}
            }
        } catch {
            $unknown++;if($errors.Count -lt 256){$errors.Add(@{path=$current;operation='enumeration';error=$_.Exception.Message})}
            if($_.Exception.Message -match 'LIMIT'){$complete=$false;break}
        }
    }
    }
    $runRefs=if(-not $referencesComplete){$null}elseif($item.kind -eq 'request' -and $references.ContainsKey($id)){$references[$id]}elseif($item.kind -eq 'resource_set' -and $setReferences.ContainsKey([IO.Path]::GetFileName($directory))){$setReferences[[IO.Path]::GetFileName($directory)]}elseif($item.kind -in @('request','resource_set')){0}else{$null}
    $requestRefs=if($item.kind -eq 'resource_set' -and $referencesComplete){if($setRequestReferences.ContainsKey([IO.Path]::GetFileName($directory))){$setRequestReferences[[IO.Path]::GetFileName($directory)]}else{0}}else{$null}
    $rows.Add(@{id=$id;kind=$item.kind;entry_type=if($item.file){'file'}else{'directory'};files=$files;logical_bytes=$logical;allocated_bytes_by_path=$allocated;
        resource_digest=if($requestSets.ContainsKey($id)){$requestSets[$id]}else{$null};
        unknown_entries=$unknown;historical_run_references=$runRefs;request_layout_references=$requestRefs;
        format_validation='Static classification and bounded marker parsing only; payload hashes/identity and active references remain unverified'})
    if($watch.Elapsed.TotalSeconds -gt $MaxSeconds -or $records -ge 100000){break}
}
$complete=$complete -and $referencesComplete -and $errors.Count -eq 0
$result=@{schema_version=2;data_root=$data;complete=$complete;elapsed_seconds=$watch.Elapsed.TotalSeconds;
    references_complete=$referencesComplete;
    entries_observed=$rows.Count;entries_total=if($complete){$rows.Count}else{$null};
    directories_observed=@($rows|Where-Object entry_type -eq 'directory').Count;
    directories_total=if($complete){@($rows|Where-Object entry_type -eq 'directory').Count}else{$null};
    records_observed=$records;record_limit=100000;object_limit=5000;
    rows=@($rows);errors=@($errors);read_only=$true;deleted=0;
    allocated_bytes_definition='Sum of GetCompressedFileSizeW storage bytes per path; uncompressed/non-sparse files return logical size. This is not cluster-rounded allocation or exclusive physical volume usage; hardlinks/dedup/shared extents may be counted more than once.';
    cluster_allocation_bytes=$null;exclusive_physical_volume_bytes=$null;
    active_lease_references='unknown: current API does not expose all BundleLease roots; historical run references are not active leases';
    resumed_entries=$resumed.Count;resumed_directories=@($resumed.Values|Where-Object entry_type -eq 'directory').Count;resumed_source=$CompletedInventoryPath;
    sampling='Sequential, non-atomic inventory; resumed complete directory samples retain their earlier observation window. No claim of one instantaneous filesystem snapshot.';
    retention='Published revisions and run/diagnostic history retained; no cleanup policy applied.'}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($output)) | Out-Null
$partial=$output+'.partial'
$file=[IO.File]::Open($partial,'CreateNew','Write','Read')
$outputBytes=0L
function Write-Bounded([string]$Text) {
    $bytes=[Text.Encoding]::UTF8.GetBytes($Text)
    if($script:outputBytes+$bytes.Length -gt 32MB){throw 'INVENTORY_OUTPUT_BYTES_LIMIT'}
    $file.Write($bytes);$script:outputBytes+=$bytes.Length
}
try {
    Write-Bounded '{'
    $first=$true
    foreach($key in $result.Keys) {
        if(-not $first){Write-Bounded ','};$first=$false
        Write-Bounded (($key|ConvertTo-Json -Compress)+':')
        if($key -in @('rows','errors')) {
            Write-Bounded '[';$firstRow=$true
            foreach($row in $result[$key]) {
                if(-not $firstRow){Write-Bounded ','};$firstRow=$false
                Write-Bounded ($row|ConvertTo-Json -Depth 6 -Compress)
            }
            Write-Bounded ']'
        } else {Write-Bounded ($result[$key]|ConvertTo-Json -Depth 6 -Compress)}
    }
    Write-Bounded "}`n"
} finally {$file.Dispose()}
[IO.File]::Move($partial,$output,$false)
[pscustomobject]@{complete=$complete;entries=$rows.Count;directories=$result.directories_observed;errors=$errors.Count;output=$output} | ConvertTo-Json -Compress
if(-not $complete -or $errors.Count){exit 1}

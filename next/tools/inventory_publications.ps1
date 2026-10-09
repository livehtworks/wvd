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
        if([string]$row.id -notmatch '^[A-Za-z0-9_-]{1,128}$'){throw 'INVENTORY_RESUME_ID_INVALID'}
        if($row.unknown_entries -eq 0 -and (Test-Path -LiteralPath (Join-Path $published $row.id) -PathType Container)) {
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
foreach($directory in [IO.Directory]::EnumerateDirectories($published)) {
    if($watch.Elapsed.TotalSeconds -gt $MaxSeconds -or $rows.Count -ge 5000){$complete=$false;break}
    $id=[IO.Path]::GetFileName($directory)
    if($resumed.ContainsKey($id)){
        $row=$resumed[$id]
        $row.historical_run_references=if(-not $referencesComplete){$null}elseif($references.ContainsKey($id)){$references[$id]}else{0}
        $rows.Add($row);continue
    }
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
                if(($attributes -band [IO.FileAttributes]::Directory) -ne 0){$stack.Push($entry);continue}
                $files++;$logical+=([IO.FileInfo]::new($entry)).Length
                try {$allocated += [long][WvdPublicationDisk]::Bytes($entry)}
                catch {$unknown++;if($errors.Count -lt 256){$errors.Add(@{path=$entry;operation='allocated_bytes';error=$_.Exception.Message})}}
            }
        } catch {
            $unknown++;if($errors.Count -lt 256){$errors.Add(@{path=$current;operation='enumeration';error=$_.Exception.Message})}
            if($_.Exception.Message -match 'LIMIT'){$complete=$false;break}
        }
    }
    $rows.Add(@{id=$id;files=$files;logical_bytes=$logical;allocated_bytes_by_path=$allocated;
        unknown_entries=$unknown;historical_run_references=if(-not $referencesComplete){$null}elseif($references.ContainsKey($id)){$references[$id]}else{0}})
    if(-not $complete){break}
}
$complete=$complete -and $referencesComplete -and $errors.Count -eq 0
$result=@{schema_version=1;data_root=$data;complete=$complete;elapsed_seconds=$watch.Elapsed.TotalSeconds;
    references_complete=$referencesComplete;
    directories_observed=$rows.Count;directories_total=if($complete){$rows.Count}else{$null};
    rows=@($rows);errors=@($errors);read_only=$true;deleted=0;
    allocated_bytes_definition='Sum of GetCompressedFileSizeW storage bytes per path; uncompressed/non-sparse files return logical size. This is not cluster-rounded allocation or exclusive physical volume usage; hardlinks/dedup/shared extents may be counted more than once.';
    cluster_allocation_bytes=$null;exclusive_physical_volume_bytes=$null;
    active_lease_references='unknown: current API does not expose all BundleLease roots; historical run references are not active leases';
    resumed_directories=$resumed.Count;resumed_source=$CompletedInventoryPath;
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
[pscustomobject]@{complete=$complete;directories=$rows.Count;errors=$errors.Count;output=$output} | ConvertTo-Json -Compress
if(-not $complete -or $errors.Count){exit 1}

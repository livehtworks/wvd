#requires -Version 7.0
Set-StrictMode -Version Latest

function Initialize-TraceInterop {
    if ('WvdTraceNative' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class WvdTraceNative {
    [StructLayout(LayoutKind.Sequential)]
    struct PerformanceInfo { public uint Size; public UIntPtr CommitTotal, CommitLimit, CommitPeak,
        PhysicalTotal, PhysicalAvailable, SystemCache, KernelTotal, KernelPaged, KernelNonpaged, PageSize;
        public uint Handles, Processes, Threads; }
    [DllImport("psapi.dll", SetLastError=true)]
    static extern bool GetPerformanceInfo(ref PerformanceInfo info, uint size);
    public static double SystemCommitRatio() {
        var info=new PerformanceInfo(); info.Size=(uint)Marshal.SizeOf<PerformanceInfo>();
        if(!GetPerformanceInfo(ref info,info.Size)) throw new Win32Exception(Marshal.GetLastWin32Error());
        if(info.CommitLimit.ToUInt64()==0) throw new Exception("SYSTEM_COMMIT_LIMIT_UNKNOWN");
        return (double)info.CommitTotal.ToUInt64()/info.CommitLimit.ToUInt64();
    }
    [StructLayout(LayoutKind.Sequential)]
    struct Wnode { public uint Size, Provider; public ulong Context; public long Time;
        public Guid Guid; public uint Clock, Flags; }
    [StructLayout(LayoutKind.Sequential)]
    struct Properties { public Wnode Wnode; public uint BufferSize, MinimumBuffers, MaximumBuffers,
        MaximumFileSize, LogFileMode, FlushTimer, EnableFlags; public int AgeLimit;
        public uint NumberOfBuffers, FreeBuffers, EventsLost, BuffersWritten, LogBuffersLost,
        RealTimeBuffersLost; public UIntPtr LoggerThread; public uint FileOffset, NameOffset; }
    public sealed class Session {
        public string Name, File; public uint EventsLost, LogBuffersLost, RealTimeBuffersLost,
            BuffersWritten, NumberOfBuffers, FreeBuffers, BufferSizeKiB;
    }
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode)]
    static extern uint QueryAllTracesW([In, Out] IntPtr[] entries, uint count, out uint needed);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode)]
    static extern uint ControlTraceW(ulong handle, string name, IntPtr properties, uint command);
    static IntPtr Allocate() {
        int bytes = 65536, size = Marshal.SizeOf<Properties>();
        var p = Marshal.AllocHGlobal(bytes);
        Marshal.Copy(new byte[bytes], 0, p, bytes);
        var props = new Properties { Wnode = new Wnode { Size = (uint)bytes },
            NameOffset = (uint)size, FileOffset = (uint)(size + 2048) };
        Marshal.StructureToPtr(props, p, false); return p;
    }
    static Session Read(IntPtr address) {
        var p = Marshal.PtrToStructure<Properties>(address);
        if (p.NameOffset >= 65536 || p.FileOffset >= 65536) throw new Exception("ETW_INVALID_OFFSET");
        return new Session { Name = Marshal.PtrToStringUni(IntPtr.Add(address, (int)p.NameOffset)),
            File = Marshal.PtrToStringUni(IntPtr.Add(address, (int)p.FileOffset)),
            EventsLost=p.EventsLost, LogBuffersLost=p.LogBuffersLost, RealTimeBuffersLost=p.RealTimeBuffersLost,
            BuffersWritten=p.BuffersWritten, NumberOfBuffers=p.NumberOfBuffers,
            FreeBuffers=p.FreeBuffers, BufferSizeKiB=p.BufferSize };
    }
    public static Session[] Discover(string instance) {
        var entries = new IntPtr[64];
        try {
            for (int i=0; i<entries.Length; ++i) entries[i]=Allocate();
            uint status=QueryAllTracesW(entries, (uint)entries.Length, out uint count);
            if (status!=0) throw new Win32Exception((int)status, "ETW_ENUMERATION_FAILED");
            var result=new List<Session>();
            for (int i=0; i<count; ++i) { var s=Read(entries[i]);
                if (s.Name.IndexOf(instance, StringComparison.OrdinalIgnoreCase)>=0) result.Add(s); }
            return result.ToArray();
        } finally { foreach(var p in entries) if(p!=IntPtr.Zero) Marshal.FreeHGlobal(p); }
    }
    public static Session Query(string name) {
        var p=Allocate();
        try { uint status=ControlTraceW(0, name, p, 0);
            if(status!=0) throw new Win32Exception((int)status, "ETW_QUERY_FAILED:"+name);
            return Read(p);
        } finally { Marshal.FreeHGlobal(p); }
    }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct SymbolInfo { public uint Size;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=261)] public string File;
        [MarshalAs(UnmanagedType.Bool)] public bool Stripped;
        public uint Timestamp, ImageSize;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=261)] public string DbgFile;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=261)] public string PdbFile;
        public Guid Guid; public uint Signature, Age; }
    [DllImport("dbghelp.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool SymSrvGetFileIndexInfoW(string path, ref SymbolInfo info, uint flags);
    public static SymbolInfo Index(string path) {
        var info = new SymbolInfo(); info.Size=(uint)Marshal.SizeOf<SymbolInfo>();
        if(!SymSrvGetFileIndexInfoW(path, ref info, 0)) throw new Win32Exception(Marshal.GetLastWin32Error());
        return info;
    }
}
'@
}

function Invoke-TraceTool {
    param([string]$Executable, [string[]]$Arguments, [string]$Log,
          [int]$TimeoutSeconds = 20, [string]$OutputRoot, [long]$OutputLimit = 128MB,
          [DateTime]$DeadlineUtc=[DateTime]::MaxValue, [scriptblock]$Cancelled,
          [long]$MaxPrivateBytes=0, [double]$MaxSystemCommitRatio=0)
    if ([DateTime]::UtcNow -ge $DeadlineUtc) { throw 'TRACE_TOOL_DEADLINE_ALREADY_EXPIRED' }
    if ($Cancelled -and (& $Cancelled)) { throw 'TRACE_TOOL_CANCELLED_BEFORE_START' }
    if ($MaxSystemCommitRatio -gt 0) {
        Initialize-TraceInterop
        if ([WvdTraceNative]::SystemCommitRatio() -ge $MaxSystemCommitRatio) { throw 'ANALYSIS_SYSTEM_COMMIT_BUDGET_NOT_AVAILABLE' }
    }
    $info = [Diagnostics.ProcessStartInfo]::new($Executable)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($arg in $Arguments) { $info.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    $started = $false
    $terminationAttempted = $false
    try {
        if (-not $process.Start()) { throw 'WPR_START_FAILED' }
        $started = $true
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $watch=[Diagnostics.Stopwatch]::StartNew()
        $failure=''
        while (-not $process.WaitForExit(200)) {
            if ($watch.Elapsed.TotalSeconds -ge $TimeoutSeconds) { $failure='TRACE_TOOL_TIMEOUT'; break }
            if ([DateTime]::UtcNow -ge $DeadlineUtc) { $failure='TRACE_TOOL_ABSOLUTE_DEADLINE'; break }
            if ($Cancelled -and (& $Cancelled)) { $failure='TRACE_TOOL_CANCELLED'; break }
            if ($MaxPrivateBytes -gt 0) {
                $process.Refresh()
                if ($process.PrivateMemorySize64 -gt $MaxPrivateBytes) { $failure='ANALYZER_PRIVATE_COMMIT_LIMIT'; break }
            }
            if ($MaxSystemCommitRatio -gt 0 -and [WvdTraceNative]::SystemCommitRatio() -ge $MaxSystemCommitRatio) {
                $failure='ANALYZER_SYSTEM_COMMIT_LIMIT'; break
            }
            if ($OutputRoot -and (Test-Path -LiteralPath $OutputRoot)) {
                $bytes=Get-TraceFileBytes -Root $OutputRoot -Filter '*'
                if ($bytes -ge $OutputLimit) { $failure='TRACE_EXPORT_OUTPUT_LIMIT'; break }
            }
        }
        if (-not $failure -and $OutputRoot -and (Test-Path -LiteralPath $OutputRoot) -and
            (Get-TraceFileBytes -Root $OutputRoot -Filter '*') -ge $OutputLimit) {
            $failure='TRACE_EXPORT_OUTPUT_LIMIT'
        }
        if ($failure) {
            if (-not $process.HasExited) {
                $terminationAttempted=$true; $process.Kill()
                if (-not $process.WaitForExit(5000)) { throw "TRACE_TOOL_CHILD_EXIT_UNCONFIRMED:$($process.Id)" }
            }
            [IO.File]::WriteAllText($Log, $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult())
            throw "$($failure):$($Arguments[0])"
        }
        $text = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText($Log, $text)
        if ($process.ExitCode -ne 0) { throw "WPR_FAILED:$($Arguments[0]):$($process.ExitCode)" }
        return $text
    } finally {
        # Log/file errors are also failures after a child may have started.
        try {
            if ($started -and -not $process.HasExited) {
                if ($terminationAttempted) { throw "TRACE_TOOL_CHILD_EXIT_UNCONFIRMED:$($process.Id)" }
                $terminationAttempted=$true; $process.Kill()
                if (-not $process.WaitForExit(5000)) { throw "TRACE_TOOL_CHILD_EXIT_UNCONFIRMED:$($process.Id)" }
            }
        } finally { $process.Dispose() }
    }
}

function Assert-TraceSnapshotBoundary {
    param($Before, $After, [ValidateSet('worker_joined','batch_payloads_released')][string]$Stage)
    foreach ($run in @($Before, $After)) {
        if ($run.state -ne 'Completed' -or -not $run.quiescent -or -not $run.repeat) {
            throw 'SNAPSHOT_BOUNDARY_NOT_QUIESCENT'
        }
        if ($Stage -eq 'worker_joined') {
            # busy belongs to the entire batch, not just this completed worker.
            if (-not $run.repeat.active -or $run.repeat.state -ne 'waiting') {
                throw 'SNAPSHOT_CROSSED_PREPARATION_OR_BATCH_RELEASE'
            }
        } elseif ($run.busy -or $run.repeat.active -or $run.repeat.state -ne 'completed') {
            throw 'SNAPSHOT_BATCH_PAYLOADS_NOT_RELEASED'
        }
    }
    if ($Before.run_id -ne $After.run_id -or $Before.generation -ne $After.generation -or
        $Before.service_instance -ne $After.service_instance -or $Before.run_directory -ne $After.run_directory -or
        $Before.repeat.request_id -ne $After.repeat.request_id -or
        $Before.repeat.completed_cycles -ne $After.repeat.completed_cycles) {
        throw 'SNAPSHOT_RUN_OR_BATCH_IDENTITY_CHANGED'
    }
}

function Invoke-TraceWpr {
    param([string[]]$Arguments, [string]$Log, [int]$TimeoutSeconds = 20,
          [DateTime]$DeadlineUtc=[DateTime]::MaxValue, [scriptblock]$Cancelled)
    Invoke-TraceTool -Executable (Join-Path $env:WINDIR 'System32/wpr.exe') -Arguments $Arguments `
        -Log $Log -TimeoutSeconds $TimeoutSeconds -DeadlineUtc $DeadlineUtc -Cancelled $Cancelled
}

function Assert-NoExistingWprCapture {
    $sessions=@([WvdTraceNative]::Discover('') | Where-Object {$_.Name -like 'WPR_initiated_*'})
    if ($sessions.Count) { throw 'EXISTING_NAMED_TRACE_NOT_OWNED' }
}

function Get-TraceFileBytes {
    param([string]$Root, [string]$Filter='*.etl')
    $total = 0L
    foreach ($file in Get-ChildItem -LiteralPath $Root -File -Recurse -Filter $Filter) {
        $stream = $null
        try {
            $stream = [IO.File]::Open($file.FullName, 'Open', 'Read',
                [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
            $total += $stream.Length
        } catch [IO.FileNotFoundException] { }
        finally { if ($stream) { $stream.Dispose() } }
    }
    return $total
}

function Read-TraceFileIntegrity {
    param([string]$AnalyzerPath, [string]$TracePath, [string]$OutputRoot)
    $directory=Join-Path $OutputRoot ('final-integrity-'+[guid]::NewGuid().ToString('N'))
    $commandFailure=''
    try {
        Invoke-TraceTool -Executable $AnalyzerPath -Arguments @('metadata','--etl',$TracePath,
            '--output',$directory,'--memory-mib','512','--output-mib','1') `
            -Log ($directory+'.log') -TimeoutSeconds 30 -OutputRoot $directory -OutputLimit 1MB | Out-Null
    } catch { $commandFailure=$_.Exception.Message }
    $path=Join-Path $directory 'trace-integrity.json'
    if (-not (Test-Path -LiteralPath $path)) { throw "FINAL_TRACE_STATISTICS_UNKNOWN:$commandFailure" }
    $value=Get-Content -Encoding utf8 -Raw -LiteralPath $path | ConvertFrom-Json
    $value | Add-Member -NotePropertyName inspection_succeeded -NotePropertyValue (-not $commandFailure)
    $value | Add-Member -NotePropertyName inspection_failure -NotePropertyValue $commandFailure
    return $value
}

function Assert-TraceFileIntegrity {
    param($Value)
    if (($null -ne $Value.events_lost -and $Value.events_lost -ne 0) -or
        ($null -ne $Value.buffers_lost -and $Value.buffers_lost -ne 0)) { throw 'FINAL_TRACE_EVENTS_OR_BUFFERS_LOST' }
    if (-not $Value.inspection_succeeded -or -not $Value.complete -or
        $null -eq $Value.events_lost -or $null -eq $Value.buffers_lost) { throw 'FINAL_TRACE_INTEGRITY_UNKNOWN_OR_INCOMPLETE' }
}

function Close-OwnedTraceCapture {
    param([hashtable]$State, [scriptblock]$Command)
    $errors=[Collections.Generic.List[string]]::new()
    $traceClosed=-not $State.trace_attempted
    $snapshotClosed=-not $State.snapshot_attempted
    if ($State.trace_attempted) {
        try {
            $sessions=@([WvdTraceNative]::Discover($State.session))
            if ($sessions.Count -and -not $State.stop_attempted) {
                $State.stop_attempted=$true
                try { & $Command @('-stop',(Join-Path $State.root 'incomplete.etl'),'-skipPdbGen','-compress','-instancename',$State.session) 'stop-incomplete' 120 | Out-Null }
                catch { $errors.Add($_.Exception.Message) }
            }
            if (@([WvdTraceNative]::Discover($State.session)).Count) {
                try { & $Command @('-cancel','-instancename',$State.session) 'cancel-owned' 20 | Out-Null }
                catch { $errors.Add($_.Exception.Message) }
            }
            $traceClosed=(@([WvdTraceNative]::Discover($State.session)).Count -eq 0)
            if (-not $traceClosed) { $errors.Add('OWNED_COLLECTOR_REMAINS') }
        } catch { $errors.Add('OWNED_TRACE_STATE_UNKNOWN:'+ $_.Exception.Message) }
    }
    if ($State.snapshot_attempted) {
        try {
            $current=Get-Process -Id $State.target.pid -ErrorAction SilentlyContinue
            if (-not $current) { $snapshotClosed=$true }
            elseif ($current.StartTime.ToFileTimeUtc().ToString() -ne $State.target.process_start_filetime) {
                $errors.Add('PID_IDENTITY_CHANGED_CONFIG_NOT_TOUCHED')
            } else {
                $actual=& $Command @('-snapshotconfig','heap','-pid',[string]$State.target.pid) 'snapshot-config-cleanup-before' 20
                if ($actual -match 'snapshot is enabled') {
                    & $Command @('-snapshotconfig','heap','-pid',[string]$State.target.pid,'disable') 'snapshot-disable' 20 | Out-Null
                } elseif ($actual -notmatch 'snapshot is disabled') { throw 'SNAPSHOT_ACTUAL_STATE_UNKNOWN' }
                $after=& $Command @('-snapshotconfig','heap','-pid',[string]$State.target.pid) 'snapshot-config-after' 20
                $snapshotClosed=($after -match 'snapshot is disabled')
                if (-not $snapshotClosed) { $errors.Add('SNAPSHOT_NOT_RESTORED') }
            }
        } catch { $errors.Add('SNAPSHOT_CLEANUP_FAILED:'+ $_.Exception.Message) }
    }
    return [pscustomobject]@{trace_closed=$traceClosed;snapshot_config_closed=$snapshotClosed;
        cleanup_confirmed=($traceClosed -and $snapshotClosed);errors=@($errors)}
}

Export-ModuleMember -Function Initialize-TraceInterop, Invoke-TraceWpr, Invoke-TraceTool, Get-TraceFileBytes, Assert-TraceSnapshotBoundary, Read-TraceFileIntegrity, Assert-TraceFileIntegrity, Close-OwnedTraceCapture, Assert-NoExistingWprCapture

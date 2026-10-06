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
          [int]$TimeoutSeconds = 20, [string]$OutputRoot, [long]$OutputLimit = 128MB)
    $info = [Diagnostics.ProcessStartInfo]::new($Executable)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($arg in $Arguments) { $info.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw 'WPR_START_FAILED' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $watch=[Diagnostics.Stopwatch]::StartNew()
        $failure=''
        while (-not $process.WaitForExit(200)) {
            if ($watch.Elapsed.TotalSeconds -ge $TimeoutSeconds) { $failure='TRACE_TOOL_TIMEOUT'; break }
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
            if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
            [IO.File]::WriteAllText($Log, $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult())
            throw "$($failure):$($Arguments[0])"
        }
        $text = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText($Log, $text)
        if ($process.ExitCode -ne 0) { throw "WPR_FAILED:$($Arguments[0]):$($process.ExitCode)" }
        return $text
    } finally { $process.Dispose() }
}

function Invoke-TraceWpr {
    param([string[]]$Arguments, [string]$Log, [int]$TimeoutSeconds = 20)
    Invoke-TraceTool -Executable (Join-Path $env:WINDIR 'System32/wpr.exe') -Arguments $Arguments `
        -Log $Log -TimeoutSeconds $TimeoutSeconds
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

Export-ModuleMember -Function Initialize-TraceInterop, Invoke-TraceWpr, Invoke-TraceTool, Get-TraceFileBytes

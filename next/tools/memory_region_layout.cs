using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;

// Read-only region metadata. No process memory contents, suspension or injection.
public static class WvdMemoryRegions
{
    [StructLayout(LayoutKind.Sequential)]
    private struct Region
    {
        public ulong Base, AllocationBase;
        public uint AllocationProtect;
        public ushort Partition;
        public ulong Size;
        public uint State, Protect, Type;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct SystemInfo
    {
        public ushort Architecture, Reserved;
        public uint PageSize;
        public ulong Minimum, Maximum, Mask;
        public uint Processors, ProcessorType, AllocationGranularity;
        public ushort Level, Revision;
    }
    public sealed class Entry
    {
        public string Address, AllocationBase;
        public ulong Bytes;
        public uint Type, Protection;
    }
    public sealed class Layout
    {
        public readonly List<Entry> Regions = new List<Entry>();
        public ulong PrivateCommitted, MappedCommitted, ImageCommitted, OtherCommitted;
        public int Examined;
        public long ElapsedMilliseconds;
    }
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern UIntPtr VirtualQueryEx(IntPtr process, UIntPtr address, out Region region, UIntPtr length);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetProcessTimes(IntPtr process, out ulong created, out ulong exited, out ulong kernel, out ulong user);
    [DllImport("kernel32.dll")]
    private static extern void GetNativeSystemInfo(out SystemInfo info);
    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr process);

    public static Layout Read(int pid, ulong expectedCreation, int maxMilliseconds)
    {
        if (IntPtr.Size != 8 || Marshal.SizeOf<Region>() != 48 || maxMilliseconds < 1 || maxMilliseconds > 5000)
            throw new ArgumentException("REGION_READ_ARGUMENTS_INVALID");
        var process = OpenProcess(0x400, false, pid);
        if (process == IntPtr.Zero) throw new Win32Exception();
        try
        {
            ulong created, exited, kernel, user;
            if (!GetProcessTimes(process, out created, out exited, out kernel, out user)) throw new Win32Exception();
            if (created != expectedCreation) throw new InvalidOperationException("REGION_PID_REUSED");
            SystemInfo info; GetNativeSystemInfo(out info);
            var result = new Layout();
            var watch = Stopwatch.StartNew();
            ulong address = 0;
            while (address <= info.Maximum)
            {
                if (++result.Examined > 32768 || watch.ElapsedMilliseconds >= maxMilliseconds)
                    throw new InvalidOperationException("REGION_SCAN_BOUND_EXCEEDED");
                Region region;
                if (VirtualQueryEx(process, (UIntPtr)address, out region, (UIntPtr)48) != (UIntPtr)48)
                    throw new Win32Exception();
                if (region.State == 0x1000)
                {
                    result.Regions.Add(new Entry { Address = "0x" + region.Base.ToString("x"),
                        AllocationBase = "0x" + region.AllocationBase.ToString("x"), Bytes = region.Size,
                        Type = region.Type, Protection = region.Protect });
                    switch (region.Type)
                    {
                        case 0x20000: result.PrivateCommitted += region.Size; break;
                        case 0x40000: result.MappedCommitted += region.Size; break;
                        case 0x1000000: result.ImageCommitted += region.Size; break;
                        default: result.OtherCommitted += region.Size; break;
                    }
                }
                var next = checked(region.Base + region.Size);
                if (next <= address) throw new InvalidOperationException("NON_ADVANCING_REGION");
                address = next;
            }
            result.ElapsedMilliseconds = watch.ElapsedMilliseconds;
            return result;
        }
        finally { CloseHandle(process); }
    }
}

using System.ComponentModel;
using System.Runtime.InteropServices;

namespace Wvd.HeapAnalyzer;

// The SDK buffers snapshots. Enforce a process commit limit before opening ETL.
internal sealed class WindowsJobLimits : IDisposable
{
    [StructLayout(LayoutKind.Sequential)]
    private struct BasicLimits
    {
        public long ProcessTime, JobTime;
        public uint Flags;
        public UIntPtr MinWorkingSet, MaxWorkingSet;
        public uint ActiveProcesses;
        public UIntPtr Affinity;
        public uint Priority, Scheduling;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct IoCounters { public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes; }
    [StructLayout(LayoutKind.Sequential)]
    private struct ExtendedLimits
    {
        public BasicLimits Basic;
        public IoCounters Io;
        public UIntPtr ProcessMemory, JobMemory, PeakProcessMemory, PeakJobMemory;
    }
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr CreateJobObjectW(IntPtr attributes, IntPtr name);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetInformationJobObject(IntPtr job, int informationClass,
        ref ExtendedLimits information, uint size);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool QueryInformationJobObject(IntPtr job, int informationClass,
        out ExtendedLimits information, uint size, IntPtr returnedSize);
    [DllImport("kernel32.dll")] private static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
    private readonly IntPtr handle;

    public WindowsJobLimits(long bytes)
    {
        if (!OperatingSystem.IsWindows() || !Environment.Is64BitProcess) throw new InvalidOperationException("X64_WINDOWS_REQUIRED");
        handle = CreateJobObjectW(IntPtr.Zero, IntPtr.Zero);
        if (handle == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        var limits = new ExtendedLimits {
            Basic = new BasicLimits { Flags = 0x100 | 0x8, ActiveProcesses = 1 },
            ProcessMemory = (UIntPtr)bytes
        };
        if (!SetInformationJobObject(handle, 9, ref limits, (uint)Marshal.SizeOf<ExtendedLimits>()) ||
            !AssignProcessToJobObject(handle, GetCurrentProcess()))
        {
            var error = Marshal.GetLastWin32Error(); CloseHandle(handle);
            throw new Win32Exception(error, "ANALYZER_MEMORY_JOB_UNAVAILABLE");
        }
    }
    public void Dispose() => CloseHandle(handle);
    public ulong PeakPrivateBytes()
    {
        if (!QueryInformationJobObject(handle, 9, out var limits,
            (uint)Marshal.SizeOf<ExtendedLimits>(), IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
        return limits.PeakProcessMemory.ToUInt64();
    }
}

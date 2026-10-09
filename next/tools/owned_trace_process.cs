using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

// Only diagnostic children belong to this job. The collector, game and other
// user's processes are never assigned to it. JOB_LIST removes the launch race.
public sealed class WvdTraceProcess : IDisposable {
    [StructLayout(LayoutKind.Sequential)] struct Security { public int size; public IntPtr descriptor; public int inherit; }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct Startup {
        public int size; public string reserved, desktop, title;
        public uint x,y,width,height,xChars,yChars,fill,flags;
        public ushort show,reservedSize; public IntPtr reservedBytes,stdin,stdout,stderr;
    }
    [StructLayout(LayoutKind.Sequential)] struct ExtendedStartup { public Startup info; public IntPtr attributes; }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo { public IntPtr process,thread; public uint pid,tid; }
    [StructLayout(LayoutKind.Sequential)] struct BasicLimit {
        public long processTime,jobTime; public uint flags; public UIntPtr minWorking,maxWorking;
        public uint activeLimit; public UIntPtr affinity; public uint priority,scheduling;
    }
    [StructLayout(LayoutKind.Sequential)] struct IoCounters { public ulong a,b,c,d,e,f; }
    [StructLayout(LayoutKind.Sequential)] struct ExtendedLimit {
        public BasicLimit basic; public IoCounters io;
        public UIntPtr processMemory,jobMemory,peakProcess,peakJob;
    }
    [StructLayout(LayoutKind.Sequential)] struct Accounting {
        public long user,kernel,periodUser,periodKernel;
        public uint faults,total,active,terminated;
    }
    [DllImport("kernel32", SetLastError=true)] static extern IntPtr CreateJobObjectW(IntPtr security,string name);
    [DllImport("kernel32", SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job,int type,ref ExtendedLimit info,int size);
    [DllImport("kernel32", SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr job,int type,out Accounting info,int size,IntPtr length);
    [DllImport("kernel32", SetLastError=true)] static extern bool TerminateJobObject(IntPtr job,uint code);
    [DllImport("kernel32", SetLastError=true)] static extern bool CreatePipe(out IntPtr read,out IntPtr write,ref Security security,uint size);
    [DllImport("kernel32", SetLastError=true)] static extern bool SetHandleInformation(IntPtr handle,uint mask,uint flags);
    [DllImport("kernel32", SetLastError=true)] static extern bool InitializeProcThreadAttributeList(IntPtr list,int count,int flags,ref IntPtr size);
    [DllImport("kernel32", SetLastError=true)] static extern bool UpdateProcThreadAttribute(IntPtr list,uint flags,IntPtr attribute,IntPtr value,IntPtr size,IntPtr previous,IntPtr returned);
    [DllImport("kernel32")] static extern void DeleteProcThreadAttributeList(IntPtr list);
    [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool CreateProcessW(string application,StringBuilder command,IntPtr processSecurity,IntPtr threadSecurity,bool inherit,uint flags,IntPtr environment,string directory,ref ExtendedStartup startup,out ProcessInfo process);
    [DllImport("kernel32", SetLastError=true)] static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
    [DllImport("kernel32", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr process,out uint code);
    [DllImport("kernel32")] static extern bool CloseHandle(IntPtr handle);
    IntPtr job,nativeProcess;
    Process process;
    public Stream StandardOutput { get; private set; }
    public Stream StandardError { get; private set; }
    public int Id { get { return process.Id; } }
    public bool HasExited { get { return WaitForExit(0); } }
    public int ExitCode { get {
        if(!HasExited) throw new InvalidOperationException("TRACE_CHILD_NOT_EXITED");
        uint code;Check(GetExitCodeProcess(nativeProcess,out code),"TRACE_CHILD_EXIT_CODE_FAILED");
        return unchecked((int)code);
    } }
    public long PrivateMemorySize64 { get { return process.PrivateMemorySize64; } }
    public void Refresh() { process.Refresh(); }
    public bool WaitForExit(int milliseconds) {
        var result=WaitForSingleObject(nativeProcess,(uint)milliseconds);
        if(result==0) return true;
        if(result==258) return false;
        throw new Win32Exception(Marshal.GetLastWin32Error(),"TRACE_CHILD_WAIT_FAILED");
    }
    static void Check(bool success,string reason) {
        if (!success) throw new Win32Exception(Marshal.GetLastWin32Error(),reason);
    }
    static void Close(ref IntPtr handle) { if(handle!=IntPtr.Zero) {CloseHandle(handle);handle=IntPtr.Zero;} }
    static string Quote(string argument) {
        var result=new StringBuilder("\""); int slashes=0;
        foreach(char value in argument) {
            if(value=='\\') {++slashes;continue;}
            if(value=='\"') result.Append('\\',slashes*2+1);
            else result.Append('\\',slashes);
            result.Append(value); slashes=0;
        }
        return result.Append('\\',slashes*2).Append('"').ToString();
    }
    public static WvdTraceProcess Start(string executable,string[] arguments) {
        var owned=new WvdTraceProcess();
        IntPtr readOut=IntPtr.Zero,writeOut=IntPtr.Zero,readErr=IntPtr.Zero,writeErr=IntPtr.Zero;
        IntPtr readIn=IntPtr.Zero,writeIn=IntPtr.Zero,attributes=IntPtr.Zero,handles=IntPtr.Zero,jobs=IntPtr.Zero;
        var info=new ProcessInfo(); bool initialized=false;
        try {
            owned.job=CreateJobObjectW(IntPtr.Zero,null); Check(owned.job!=IntPtr.Zero,"TRACE_JOB_CREATE_FAILED");
            var limits=new ExtendedLimit(); limits.basic.flags=0x2000;
            Check(SetInformationJobObject(owned.job,9,ref limits,Marshal.SizeOf<ExtendedLimit>()),"TRACE_JOB_LIMIT_FAILED");
            var security=new Security {size=Marshal.SizeOf<Security>(),inherit=1};
            Check(CreatePipe(out readOut,out writeOut,ref security,0),"TRACE_STDOUT_PIPE_FAILED");
            Check(CreatePipe(out readErr,out writeErr,ref security,0),"TRACE_STDERR_PIPE_FAILED");
            Check(CreatePipe(out readIn,out writeIn,ref security,0),"TRACE_STDIN_PIPE_FAILED");
            Check(SetHandleInformation(readOut,1,0) && SetHandleInformation(readErr,1,0) &&
                SetHandleInformation(writeIn,1,0),"TRACE_PIPE_INHERIT_FAILED");
            IntPtr size=IntPtr.Zero; InitializeProcThreadAttributeList(IntPtr.Zero,2,0,ref size);
            attributes=Marshal.AllocHGlobal(size);
            Check(InitializeProcThreadAttributeList(attributes,2,0,ref size),"TRACE_ATTRIBUTES_FAILED"); initialized=true;
            handles=Marshal.AllocHGlobal(IntPtr.Size*3); jobs=Marshal.AllocHGlobal(IntPtr.Size);
            Marshal.WriteIntPtr(handles,0,readIn); Marshal.WriteIntPtr(handles,IntPtr.Size,writeOut);
            Marshal.WriteIntPtr(handles,IntPtr.Size*2,writeErr); Marshal.WriteIntPtr(jobs,owned.job);
            Check(UpdateProcThreadAttribute(attributes,0,new IntPtr(0x20002),handles,new IntPtr(IntPtr.Size*3),IntPtr.Zero,IntPtr.Zero),"TRACE_HANDLE_LIST_FAILED");
            Check(UpdateProcThreadAttribute(attributes,0,new IntPtr(0x2000d),jobs,new IntPtr(IntPtr.Size),IntPtr.Zero,IntPtr.Zero),"TRACE_JOB_LIST_FAILED");
            var startup=new ExtendedStartup(); startup.info.size=Marshal.SizeOf<ExtendedStartup>();
            startup.info.flags=0x100; startup.info.stdin=readIn; startup.info.stdout=writeOut; startup.info.stderr=writeErr;
            startup.attributes=attributes;
            var command=new StringBuilder(Quote(executable));
            // cmd /s /c consumes one raw command, not CRT-escaped argv. Keep its
            // shell quoting intact while retaining the same atomic Job owner.
            if(Path.GetFileName(executable).Equals("cmd.exe",StringComparison.OrdinalIgnoreCase) &&
                arguments.Length==4 && arguments[0]=="/d" && arguments[1]=="/s" && arguments[2]=="/c")
                command.Append(" /d /s /c \"").Append(arguments[3]).Append('"');
            else foreach(var argument in arguments) command.Append(' ').Append(Quote(argument));
            Check(CreateProcessW(executable,command,IntPtr.Zero,IntPtr.Zero,true,0x08080004,IntPtr.Zero,null,ref startup,out info),"TRACE_CHILD_CREATE_FAILED");
            owned.process=Process.GetProcessById((int)info.pid);
            owned.nativeProcess=info.process; info.process=IntPtr.Zero;
            owned.StandardOutput=new FileStream(new SafeFileHandle(readOut,true),FileAccess.Read,16384,false); readOut=IntPtr.Zero;
            owned.StandardError=new FileStream(new SafeFileHandle(readErr,true),FileAccess.Read,16384,false); readErr=IntPtr.Zero;
            Close(ref writeOut); Close(ref writeErr); Close(ref readIn); Close(ref writeIn);
            Check(ResumeThread(info.thread)!=uint.MaxValue,"TRACE_CHILD_RESUME_FAILED");
            return owned;
        } catch { owned.Dispose(); throw; }
        finally {
            Close(ref info.process); Close(ref info.thread);
            Close(ref readOut); Close(ref writeOut); Close(ref readErr); Close(ref writeErr); Close(ref readIn); Close(ref writeIn);
            if(initialized) DeleteProcThreadAttributeList(attributes);
            if(attributes!=IntPtr.Zero) Marshal.FreeHGlobal(attributes);
            if(handles!=IntPtr.Zero) Marshal.FreeHGlobal(handles);
            if(jobs!=IntPtr.Zero) Marshal.FreeHGlobal(jobs);
        }
    }
    public void Kill(bool tree) { CloseOwnedTree(); }
    public void CloseOwnedTree() {
        if(job==IntPtr.Zero) return;
        Check(TerminateJobObject(job,1),"TRACE_JOB_TERMINATE_FAILED");
        var watch=Stopwatch.StartNew();
        while(watch.ElapsedMilliseconds<1000) {
            Accounting accounting;
            Check(QueryInformationJobObject(job,1,out accounting,Marshal.SizeOf<Accounting>(),IntPtr.Zero),"TRACE_JOB_EXIT_QUERY_FAILED");
            if(accounting.active==0) return;
            System.Threading.Thread.Sleep(10);
        }
        throw new InvalidOperationException("TRACE_OWNED_TREE_EXIT_UNCONFIRMED");
    }
    public void Dispose() {
        try { CloseOwnedTree(); }
        finally {
            Close(ref job);
            Close(ref nativeProcess);
            if(StandardOutput!=null) StandardOutput.Dispose();
            if(StandardError!=null) StandardError.Dispose();
            if(process!=null) process.Dispose();
        }
    }
}

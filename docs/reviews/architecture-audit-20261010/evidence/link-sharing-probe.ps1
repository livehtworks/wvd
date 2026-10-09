param([string]$Root)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path $Root | Out-Null
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
public static class LinkProbe {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr CreateFile(string p, uint access, uint sharing, IntPtr s, uint disposition, uint flags, IntPtr t);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 [DllImport("ntdll.dll")] static extern int NtSetInformationFile(IntPtr h, IntPtr ios, IntPtr info, uint size, uint cls);
 public static string Run(string root, bool lockSourceParent, bool lockTargetParent) {
  var src=Path.Combine(root,"source"); var dst=Path.Combine(root,"target");
  Directory.CreateDirectory(src); Directory.CreateDirectory(dst); File.WriteAllText(Path.Combine(src,"file"),"frozen");
  var file=CreateFile(Path.Combine(src,"file"),0x80000000,1,IntPtr.Zero,3,0,IntPtr.Zero);
  var p1=lockSourceParent?CreateFile(src,0x80000000,1,IntPtr.Zero,3,0x02000000,IntPtr.Zero):IntPtr.Zero;
  var p2=lockTargetParent?CreateFile(dst,0x80000002,3,IntPtr.Zero,3,0x02000000,IntPtr.Zero):IntPtr.Zero;
  var name=System.Text.Encoding.Unicode.GetBytes(lockTargetParent?"file":"\\??\\"+Path.Combine(dst,"file"));
  var offset=IntPtr.Size==8?20:12; var info=Marshal.AllocHGlobal(offset+name.Length); var ios=Marshal.AllocHGlobal(16);
  try { for(int i=0;i<offset;i++) Marshal.WriteByte(info,i,0); Marshal.WriteIntPtr(info,IntPtr.Size==8?8:4,p2); Marshal.WriteInt32(info,offset-4,name.Length); Marshal.Copy(name,0,IntPtr.Add(info,offset),name.Length);
   return $"source_parent={lockSourceParent} target_parent={lockTargetParent} status=0x{NtSetInformationFile(file,ios,info,(uint)(offset+name.Length),11):X8}";
  } finally { Marshal.FreeHGlobal(info); Marshal.FreeHGlobal(ios); if(p2!=IntPtr.Zero)CloseHandle(p2); if(p1!=IntPtr.Zero)CloseHandle(p1); CloseHandle(file); }
 }
}
'@
foreach($source in @($false,$true)) { foreach($target in @($false,$true)) {
 [LinkProbe]::Run((Join-Path $Root "$source-$target"),$source,$target)
}}

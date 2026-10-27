"""Actual manager and filesystem tests; launcher/backend are isolated local fixtures."""

import ctypes
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
import unittest
import urllib.request
import uuid

NEXT = Path(__file__).resolve().parents[1]
MANAGER = NEXT / "tools/manage_service.ps1"
EVIDENCE = NEXT / ".local/t0-repair-261025"


def write_candidate(target, executable=b"isolated executable bytes"):
    """A small, fully consistent static candidate; no real DLL or model is loaded."""
    target.mkdir(parents=True, exist_ok=True)
    paths = {"automationd.exe": executable, "wvd-capture-host.exe": b"host",
        "opencv_world4120.dll": b"opencv", "onnxruntime.dll": b"ort",
        "scrcpy-server-v3.3.4": b"scrcpy", "web/index.html": b"<html>fixture</html>",
        "pack/parameters/semantic-assets.json": b'{"resources":{}}',
        "data/quest.json": b"{}", "tools/manage_service.ps1": MANAGER.read_bytes()}
    digest = lambda data: hashlib.sha256(data).hexdigest()
    manifest = {"revision": "fixture-pack", "files": [{"path": "parameters/semantic-assets.json",
        "sha256": digest(paths["pack/parameters/semantic-assets.json"])}]}
    paths["pack/manifest.json"] = json.dumps(manifest).encode()
    for relative, content in paths.items():
        path = target / relative; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(content)
    source = {"source_commit": "1" * 40, "product_inputs_sha256": "2" * 64, "product_input_count": 1}
    binaries = ("automationd.exe", "wvd-capture-host.exe", "opencv_world4120.dll",
                "onnxruntime.dll", "scrcpy-server-v3.3.4")
    artifacts = {"build/Release/" + name: digest(paths[name]) for name in binaries}
    artifacts["web/dist/index.html"] = digest(paths["web/index.html"])
    marker = {**source, "engine": "wvd_native", "status": "BUILT_NOT_GAME_ACCEPTED",
        "build_input": {"state": "BUILT", "source": source, "artifacts": artifacts},
        "version_output": "wvd-binding-fixture", "exe_sha256": digest(executable),
        "web_index_sha256": digest(paths["web/index.html"]), "pack_revision": manifest["revision"],
        "pack_manifest_sha256": digest(paths["pack/manifest.json"]),
        "resource_catalog_sha256": digest(paths["pack/parameters/semantic-assets.json"]),
        "files": [{"path": name, "sha256": digest(content)} for name, content in sorted(paths.items())]}
    (target / "DELIVERY_STATUS.json").write_text(json.dumps(marker), encoding="utf-8")


LAUNCHER_SOURCE = r'''
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Runtime.InteropServices;
using System.Web.Script.Serialization;
class Fixture {
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct Startup {
  public int cb; public string reserved,desktop,title;
  public uint x,y,xsize,ysize,xchars,ychars,fill,flags;
  public ushort show,reservedsize; public IntPtr reservedptr,input,output,error;
 }
 [StructLayout(LayoutKind.Sequential)] struct Child { public IntPtr process,thread; public uint pid,tid; }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool CreateProcessW(
  string application,StringBuilder command,IntPtr ps,IntPtr ts,bool inherit,uint flags,IntPtr environment,
  string directory,ref Startup startup,out Child child);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 static JavaScriptSerializer Json = new JavaScriptSerializer();
 static string Quote(string s) { return "\"" + s.Replace("\"", "\\\"") + "\""; }
 static int Main(string[] args) {
  string exe = System.Reflection.Assembly.GetExecutingAssembly().Location;
  string root = Path.GetDirectoryName(exe);
  if (args.Length == 1 && args[0] == "--version") { Console.WriteLine("wvd-binding-fixture"); return 0; }
  if (args.Length > 0 && args[0] == "--launch") {
   string mode = File.ReadAllText(Path.Combine(root, "mode.txt")).Trim();
   if (mode == "launcher_fail") return 13;
   string data = args[Array.IndexOf(args, "--data-root")+1];
   int port = Int32.Parse(args[Array.IndexOf(args, "--port")+1]);
   string binding = Path.Combine(root,"service-launch.json");
   File.AppendAllText(Path.Combine(root,"fixture-launches.jsonl"), Json.Serialize(new {
     data_root=data, old_binding=File.Exists(binding)?File.ReadAllText(binding):null,
     at=DateTime.UtcNow.Ticks})+Environment.NewLine);
   // Explicit non-inheritance prevents the backend from holding any outer
   // Python/PowerShell capture pipe, including non-standard inherited handles.
   var startup=new Startup();startup.cb=Marshal.SizeOf(typeof(Startup));Child child;
   var command=new StringBuilder(Quote(exe)+" --serve "+Quote(data)+" "+port+" "+mode);
   if(!CreateProcessW(exe,command,IntPtr.Zero,IntPtr.Zero,false,0x08000000,IntPtr.Zero,root,ref startup,out child))return 14;
   CloseHandle(child.process);CloseHandle(child.thread);
   File.AppendAllText(Path.Combine(root,"fixture-pids.txt"),child.pid+Environment.NewLine);
   Console.WriteLine(Json.Serialize(new {pid=child.pid})); return 0;
  }
  if (args.Length > 0 && args[0] == "--serve") {
   string data=args[1], mode=args[3]; int port=Int32.Parse(args[2]);
   if (mode == "early_exit") return 0;
   if (mode == "timeout") {Thread.Sleep(30000);return 0;}
   if (mode == "delay") Thread.Sleep(1500);
   var process=Process.GetCurrentProcess();
   var server=new TcpListener(IPAddress.Loopback,port); server.Start();
   while(true) {
    using(var client=server.AcceptTcpClient()) using(var stream=client.GetStream()) {
     var reader=new StreamReader(stream,Encoding.ASCII,false,1024,true);
     string line; do {line=reader.ReadLine();} while(line!=null && line.Length!=0);
     var body=Json.Serialize(new {service="automationd", state="running",
       data_root=mode=="owner_mismatch"?data+"-wrong":data,
       executable=exe, pid=mode=="pid_mismatch"?Int32.MaxValue:process.Id,
       process_start_filetime=process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString(),
       instance_id="fixture-"+process.Id});
     byte[] bytes=Encoding.UTF8.GetBytes(body);
     byte[] headers=Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+bytes.Length+"\r\nConnection: close\r\n\r\n");
     stream.Write(headers,0,headers.Length);stream.Write(bytes,0,bytes.Length);stream.Flush();
    }
   }
  }
  return 1;
 }
}
'''


@unittest.skipUnless(os.name == "nt", "Windows manager and NTFS sharing tests")
class ServiceBindingContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = EVIDENCE / ("wp05-" + uuid.uuid4().hex); cls.root.mkdir(parents=True)
        source = cls.root / "fixture.cs"; source.write_text(LAUNCHER_SOURCE, encoding="utf-8")
        compiler = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Microsoft.NET/Framework64/v4.0.30319/csc.exe"
        completed = subprocess.run([str(compiler), "/nologo", "/r:System.Web.Extensions.dll",
            "/out:" + str(cls.root / "fixture.exe"), str(source)], capture_output=True, timeout=30)
        if completed.returncode:
            raise AssertionError(completed.stdout.decode(errors="replace") + completed.stderr.decode(errors="replace"))
        cls.executable = (cls.root / "fixture.exe").read_bytes()

    def setUp(self):
        self.case = self.root / (self._testMethodName + "-" + uuid.uuid4().hex[:8]); self.case.mkdir()
        self.candidate = self.case / "candidate"; write_candidate(self.candidate, self.executable)
        self.binding = self.candidate / "service-launch.json"
        self.before = json.dumps({"service": "automationd", "data_root": str(self.case / "old-data"), "port": 17764}, indent=3).encode() + b"\n"
        self.binding.write_bytes(self.before)
        info = self.binding.stat()
        self.before_identity = [info.st_dev, info.st_ino]

    def tearDown(self):
        pids = self.candidate / "fixture-pids.txt"
        owned_pids = []
        if pids.exists():
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.OpenProcess.restype = ctypes.c_void_p
            kernel.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
            kernel.QueryFullProcessImageNameW.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_uint32)]
            kernel.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            for pid in map(int, pids.read_text().splitlines()):
                owned_pids.append(pid)
                handle = kernel.OpenProcess(0x1000 | 1 | 0x100000, False, pid)
                if not handle: continue
                try:
                    buffer = ctypes.create_unicode_buffer(32768); size = ctypes.c_uint32(len(buffer))
                    self.assertTrue(kernel.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)))
                    self.assertEqual(Path(buffer.value).resolve(), (self.candidate / "automationd.exe").resolve())
                    kernel.TerminateProcess(handle, 0); kernel.WaitForSingleObject(handle, 10000)
                finally: kernel.CloseHandle(handle)
        plain_binding = self.binding.is_file() and not self.binding.is_symlink() and not self.binding.is_junction()
        info = self.binding.lstat() if self.binding.exists() else None
        result = self._outcome.result
        failed = any(test is self for test, _ in result.failures + result.errors)
        record = {"case": self._testMethodName, "candidate": str(self.candidate),
            "before": {"identity": self.before_identity, "sha256": hashlib.sha256(self.before).hexdigest()},
            "after": {"identity": [info.st_dev, info.st_ino] if info else None,
                "sha256": hashlib.sha256(self.binding.read_bytes()).hexdigest() if plain_binding else None,
                "plain_file": plain_binding}, "fixture_pids": owned_pids,
            "fixture_cleanup": "Identity-checked held process handles only", "passed": not failed}
        (self.case / "evidence.json").write_text(json.dumps(record, indent=2), encoding="utf-8")

    @staticmethod
    def free_port():
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0)); return sock.getsockname()[1]

    def command(self, data=None, port=None):
        return ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(MANAGER),
                "-Action", "Start", "-CandidateRoot", str(self.candidate),
                "-DataRoot", str(data or self.case / "new-data"), "-Port", str(port or self.free_port()),
                "-WaitSeconds", "5"]

    def invoke(self, mode="ready"):
        (self.candidate / "mode.txt").write_text(mode)
        result = subprocess.run(self.command(), capture_output=True, timeout=30)
        (self.case / "stdout.log").write_bytes(result.stdout); (self.case / "stderr.log").write_bytes(result.stderr)
        return result

    def test_launcher_nonzero_preserves_previous_bytes(self):
        result = self.invoke("launcher_fail")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"SERVICE_LAUNCH_FAILED", result.stderr)
        self.assertIn(b"process_started=unconfirmed", result.stderr)
        self.assertEqual(self.binding.read_bytes(), self.before)

    def test_missing_data_root_has_no_appdata_fallback(self):
        self.binding.unlink()  # This test's own fresh candidate binding only.
        command = self.command()
        index = command.index("-DataRoot")
        del command[index:index + 2]
        result = subprocess.run(command, capture_output=True, timeout=20)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"SERVICE_DATA_ROOT_REQUIRED", result.stderr)
        self.assertFalse((self.case / "new-data").exists())
        self.assertFalse((self.candidate / "fixture-pids.txt").exists())

    def test_missing_bound_data_root_is_not_recreated(self):
        command = self.command()
        index = command.index("-DataRoot")
        del command[index:index + 2]
        result = subprocess.run(command, capture_output=True, timeout=20)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"SERVICE_BOUND_DATA_ROOT_MISSING", result.stderr)
        self.assertFalse((self.case / "old-data").exists())
        self.assertEqual(self.binding.read_bytes(), self.before)
        self.assertFalse((self.candidate / "fixture-pids.txt").exists())

    def test_volume_root_is_rejected_before_launcher(self):
        result = subprocess.run(self.command(data=Path(self.case.anchor)), capture_output=True, timeout=20)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"SERVICE_DATA_ROOT_MUST_NOT_BE_VOLUME_ROOT", result.stderr)
        self.assertEqual(self.binding.read_bytes(), self.before)
        self.assertFalse((self.candidate / "fixture-pids.txt").exists())

    def test_background_early_exit_preserves_previous_bytes(self):
        result = self.invoke("early_exit")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"process_started=true", result.stderr)
        self.assertEqual(self.binding.read_bytes(), self.before)

    def test_timeout_preserves_previous_bytes(self):
        result = self.invoke("timeout")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"SERVICE_START_TIMEOUT", result.stderr)
        self.assertIn(b"process_started=true", result.stderr)
        self.assertEqual(self.binding.read_bytes(), self.before)

    def test_owner_and_pid_mismatch_preserve_previous_bytes(self):
        for mode in ("owner_mismatch", "pid_mismatch"):
            result = self.invoke(mode)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"process_started=true", result.stderr)
            self.assertEqual(self.binding.read_bytes(), self.before)

    def test_success_commits_then_repeat_start_keeps_exact_binding(self):
        (self.candidate / "mode.txt").write_text("ready")
        command = self.command()
        first = subprocess.run(command, capture_output=True, timeout=30)
        self.assertEqual(first.returncode, 0, first.stderr.decode(errors="replace"))
        after = self.binding.read_bytes(); self.assertNotEqual(after, self.before)
        second = subprocess.run(command, capture_output=True, timeout=30)
        self.assertEqual(second.returncode, 0, second.stderr.decode(errors="replace"))
        self.assertEqual(self.binding.read_bytes(), after)
        self.assertEqual(len((self.candidate / "fixture-pids.txt").read_text().splitlines()), 1)
        # A known existing binding remains usable without repeating its paths.
        implicit = list(command)
        for option in ("-DataRoot", "-Port"):
            index = implicit.index(option)
            del implicit[index:index + 2]
        third = subprocess.run(implicit, capture_output=True, timeout=30)
        self.assertEqual(third.returncode, 0, third.stderr.decode(errors="replace"))
        self.assertEqual(self.binding.read_bytes(), after)
        self.assertEqual(len((self.candidate / "fixture-pids.txt").read_text().splitlines()), 1)

    def test_real_rename_share_denial_preserves_old_bytes_and_reports_process(self):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
            ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.CreateFileW(str(self.binding), 0x80000000, 1, None, 3, 0, None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
        try: result = self.invoke()
        finally: kernel.CloseHandle(handle)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"SERVICE_BINDING_COMMIT_FAILED", result.stderr)
        self.assertIn(b"process_started=true", result.stderr)
        self.assertEqual(self.binding.read_bytes(), self.before)
        self.assertFalse(list(self.candidate.glob("service-launch.json.*.tmp")))

    def test_candidate_lock_serializes_different_data_roots(self):
        (self.candidate / "mode.txt").write_text("delay")
        first_data, second_data = self.case / "first-data", self.case / "second-data"
        first = subprocess.Popen(self.command(first_data), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        launches = self.candidate / "fixture-launches.jsonl"
        deadline = time.monotonic() + 15
        while not launches.exists() and first.poll() is None and time.monotonic() < deadline: time.sleep(.05)
        self.assertTrue(launches.exists(), "First launcher never reached fixture")
        second = subprocess.Popen(self.command(second_data), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, stderr = first.communicate(timeout=30); self.assertEqual(first.returncode, 0, stderr.decode(errors="replace"))
        stdout2, stderr2 = second.communicate(timeout=30); self.assertEqual(second.returncode, 0, stderr2.decode(errors="replace"))
        entries = [json.loads(line) for line in launches.read_text().splitlines()]
        self.assertEqual(len(entries), 2)
        self.assertEqual(json.loads(entries[1]["old_binding"])["data_root"], str(first_data))
        self.assertEqual(json.loads(self.binding.read_text())["data_root"], str(second_data))

    def test_write_flush_and_rename_faults_use_real_commit_function(self):
        # Extract the actual function ASTs; only the three filesystem boundary
        # helpers are replaceable here. There is no production fault switch.
        driver = self.case / "commit-fault.ps1"
        driver.write_text(r'''param($Manager,$Candidate,$Data,[int]$Port,$Fault)
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($Manager,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'MANAGER_PARSE_FAILED'}
$names=@('Get-CandidateHash','Assert-PlainFile','Assert-Owner','Write-LaunchBindingBytes',
         'Flush-LaunchBindingBytes','Publish-LaunchBindingBytes','Commit-LaunchBinding')
$functions=$ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -in $names},$true)
foreach($function in $functions){Invoke-Expression $function.Extent.Text}
$CandidateRoot=$Candidate;$DataRoot=$Data;$launchPath=Join-Path $Candidate 'service-launch.json'
$bindingExisted=$true;$bindingHash=Get-CandidateHash $launchPath
$launch=Get-Content -LiteralPath $launchPath -Raw -Encoding UTF8|ConvertFrom-Json
$service=Invoke-RestMethod "http://127.0.0.1:$Port/api/v1/service" -TimeoutSec 5
if($Fault -eq 'write'){function Write-LaunchBindingBytes($stream,[byte[]]$bytes){throw 'TEST_BINDING_WRITE_FAILURE'}}
if($Fault -eq 'flush'){function Flush-LaunchBindingBytes($stream){throw 'TEST_BINDING_FLUSH_FAILURE'}}
if($Fault -eq 'rename'){function Publish-LaunchBindingBytes($temporary,$target,$replace){throw 'TEST_BINDING_RENAME_FAILURE'}}
Commit-LaunchBinding $service
''', encoding="utf-8")
        (self.candidate / "mode.txt").write_text("ready")
        data, port = self.case / "commit-data", self.free_port(); data.mkdir()
        launcher = subprocess.run([str(self.candidate / "automationd.exe"), "--launch", "--data-root", str(data),
                                   "--port", str(port)], capture_output=True, timeout=10)
        self.assertEqual(launcher.returncode, 0, launcher.stderr.decode(errors="replace"))
        deadline = time.monotonic() + 10
        while True:
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/api/v1/service", timeout=1) as response:
                    json.load(response)
                break
            except OSError:
                if time.monotonic() >= deadline: raise
                time.sleep(.05)
        for operation in ("write", "flush", "rename"):
            result = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(driver),
                str(MANAGER), str(self.candidate), str(data), str(port), operation], capture_output=True, timeout=15)
            (self.case / (operation + ".stderr.log")).write_bytes(result.stderr)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(("TEST_BINDING_" + operation.upper() + "_FAILURE").encode(), result.stderr)
            self.assertIn(b"SERVICE_BINDING_COMMIT_FAILED", result.stderr)
            self.assertEqual(self.binding.read_bytes(), self.before)
            self.assertFalse(list(self.candidate.glob("service-launch.json.*.tmp")))

    def test_pending_package_transaction_rejects_before_launcher(self):
        transactions = self.case / ".candidate.transactions"; transactions.mkdir()
        (transactions / "transaction.json").write_bytes(b"indexed interrupted candidate")
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"CANDIDATE_TRANSACTION_PENDING", result.stderr)
        self.assertFalse((self.candidate / "fixture-pids.txt").exists())
        self.assertEqual(self.binding.read_bytes(), self.before)

    def test_pending_package_transaction_does_not_block_explicit_stop(self):
        transactions = self.case / ".candidate.transactions"; transactions.mkdir()
        journal = transactions / "transaction.json"; journal.write_bytes(b"preserve unfinished package evidence")
        command = self.command(); command[command.index("-Action") + 1] = "Stop"
        result = subprocess.run(command, capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        self.assertEqual(journal.read_bytes(), b"preserve unfinished package evidence")
        self.assertEqual(self.binding.read_bytes(), self.before)
        self.assertFalse((self.candidate / "fixture-pids.txt").exists())

    def test_binding_reparse_is_rejected_before_launcher(self):
        outside = self.case / "external-binding"; outside.mkdir()
        (outside / "marker").write_bytes(b"preserve")
        self.binding.unlink()
        result = subprocess.run(["cmd", "/c", "mklink", "/J", str(self.binding), str(outside)],
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"CANDIDATE_FILE_IDENTITY_INVALID", result.stderr)
        self.assertFalse((self.candidate / "fixture-pids.txt").exists())
        self.assertEqual((outside / "marker").read_bytes(), b"preserve")

    def test_transaction_root_junction_cannot_create_outside_lock(self):
        outside = self.case / "outside-root"; outside.mkdir()
        transactions = self.case / ".candidate.transactions"
        made = subprocess.run(["cmd", "/c", "mklink", "/J", str(transactions), str(outside)],
                              capture_output=True, timeout=10)
        self.assertEqual(made.returncode, 0, made.stderr.decode(errors="replace"))
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"CANDIDATE_DIRECTORY_IDENTITY_INVALID", result.stderr)
        self.assertEqual(list(outside.iterdir()), [])
        self.assertEqual(self.binding.read_bytes(), self.before)

    def test_lock_leaf_reparse_is_rejected_before_launcher(self):
        outside = self.case / "outside-lock"; outside.mkdir()
        (outside / "marker").write_bytes(b"preserve")
        transactions = self.case / ".candidate.transactions"; transactions.mkdir()
        made = subprocess.run(["cmd", "/c", "mklink", "/J", str(transactions / "writer.lock"), str(outside)],
                              capture_output=True, timeout=10)
        self.assertEqual(made.returncode, 0, made.stderr.decode(errors="replace"))
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"CANDIDATE_FILE_IDENTITY_INVALID", result.stderr)
        self.assertFalse((self.candidate / "fixture-pids.txt").exists())
        self.assertEqual((outside / "marker").read_bytes(), b"preserve")


if __name__ == "__main__":
    unittest.main()

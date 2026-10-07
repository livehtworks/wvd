using System.Text.Json;
using Wvd.HeapAnalyzer;
if (args.Length != 1 || Directory.Exists(args[0])) throw new ArgumentException("NEW_ISOLATED_DIRECTORY_REQUIRED");
var root = Path.GetFullPath(args[0]);
Directory.CreateDirectory(root);
var writer = new ReportFiles(root, 1024);
bool rejected = false;
try { writer.Write("utf8.txt", w => w.Write(new string('\u4e2d', 400))); }
catch (IOException e) when (e.Message == "ANALYZER_OUTPUT_BUDGET_EXCEEDED") { rejected = true; }
if (!rejected || File.Exists(Path.Combine(root, "utf8.txt")) ||
    !File.Exists(Path.Combine(root, "utf8.txt.partial")) || writer.DirectoryBytes() > 1024)
    throw new Exception("UTF8_OUTPUT_LIMIT_NOT_ENFORCED");
using var job = new WindowsJobLimits(128L * 1024 * 1024);
bool memoryRejected = false;
var blocks = new List<byte[]>();
try { for (var i = 0; i < 32; i++) { var bytes = new byte[16 * 1024 * 1024]; bytes[0] = 1; blocks.Add(bytes); } }
catch (OutOfMemoryException) { memoryRejected = true; }
var peak = job.PeakPrivateBytes();
blocks.Clear();
if (!memoryRejected || peak > 128UL * 1024 * 1024) throw new Exception("PROCESS_COMMIT_JOB_NOT_ENFORCED");
Console.WriteLine(JsonSerializer.Serialize(new { utf8_limit_enforced = true, partial_retained = true,
    job_memory_limit_enforced = true, peak_private_commit_bytes = peak }));

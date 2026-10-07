using Microsoft.Windows.EventTracing;
using Microsoft.Windows.EventTracing.Symbols;
using System.Diagnostics;
using System.Text.Json;
using Wvd.HeapAnalyzer;

if (args.Length < 1 || args[0] is not ("metadata" or "analyze")) throw new ArgumentException("MODE_MUST_BE_METADATA_OR_ANALYZE");
var options = new Dictionary<string, string>();
for (var i = 1; i < args.Length; i += 2)
{
    if (i + 1 >= args.Length || !args[i].StartsWith("--") || !options.TryAdd(args[i], args[i + 1])) throw new ArgumentException("INVALID_OPTIONS");
}
var allowed = new[] { "--etl", "--output", "--pid", "--memory-mib", "--output-mib", "--pdb-directory", "--capture-receipt", "--target-filetime", "--target-image", "--checkpoints" };
if (options.Keys.Any(k => !allowed.Contains(k))) throw new ArgumentException("UNKNOWN_OPTION");
var root = Path.GetFullPath(options["--output"]);
if (Directory.Exists(root)) throw new IOException("OUTPUT_ALREADY_EXISTS");
var memoryMiB = int.Parse(options.GetValueOrDefault("--memory-mib", "1024"));
var outputMiB = int.Parse(options.GetValueOrDefault("--output-mib", "128"));
if (memoryMiB < 128 || memoryMiB > 4096 || outputMiB < 1 || outputMiB > 128) throw new ArgumentException("INVALID_BUDGET");
Directory.CreateDirectory(root);
var files = new ReportFiles(root, outputMiB * 1024L * 1024);
var watch = Stopwatch.StartNew();
var phase = "start";
files.Json("started.json", new { complete = false, pid = Environment.ProcessId, memory_limit_mib = memoryMiB, output_limit_mib = outputMiB });
try
{
    using var job = new WindowsJobLimits(memoryMiB * 1024L * 1024);
    using var trace = TraceProcessor.Create(Path.GetFullPath(options["--etl"]), new TraceProcessorSettings { AllowLostEvents = true });
    var metadata = trace.UseMetadata();
    if (args[0] == "analyze") trace.UseProcesses();
    var heap = args[0] == "analyze" ? trace.UseHeapSnapshots() : null;
    var symbols = args[0] == "analyze" && options.ContainsKey("--pdb-directory") ? trace.UseSymbols() : null;
    phase = "read";
    trace.Process();
    var integrity = new { complete = metadata.StopTime > metadata.StartTime,
        events_lost = metadata.LostEventCount, buffers_lost = metadata.LostBufferCount,
        start_utc = metadata.StartTime, stop_utc = metadata.StopTime };
    files.Json("trace-integrity.json", integrity);
    if (!integrity.complete || integrity.events_lost != 0 || integrity.buffers_lost != 0) throw new InvalidDataException("FINAL_TRACE_INTEGRITY_FAILED");
    if (heap != null)
    {
        phase = "aggregate";
        var stacks = new Dictionary<string, StackRecord>();
        var snapshots = HeapAggregation.Read(heap.Result, int.Parse(options["--pid"]),
            long.Parse(options["--target-filetime"]), options["--target-image"], stacks);
        if (options.TryGetValue("--checkpoints", out var checkpointsPath))
        {
            using var checkpoints = JsonDocument.Parse(File.ReadAllText(checkpointsPath));
            var entries = checkpoints.RootElement.ValueKind == JsonValueKind.Array
                ? checkpoints.RootElement.EnumerateArray().ToArray() : [checkpoints.RootElement];
            snapshots = snapshots.Select(s => {
                var time = DateTimeOffset.Parse(s.Utc);
                var matches = entries.Where(c => time >= DateTimeOffset.Parse(c.GetProperty("began_utc").GetString()!) &&
                    time <= DateTimeOffset.Parse(c.GetProperty("ended_utc").GetString()!)).ToArray();
                if (matches.Length != 1) throw new InvalidDataException("SNAPSHOT_TIMESTAMP_NOT_UNIQUELY_BOUND");
                if (matches[0].GetProperty("pid").GetInt32() != int.Parse(options["--pid"]) ||
                    matches[0].GetProperty("process_start_filetime").GetString() != options["--target-filetime"])
                    throw new InvalidDataException("CHECKPOINT_PROCESS_IDENTITY_MISMATCH");
                return s with { Phase = matches[0].GetProperty("phase").GetString()! };
            }).ToList();
        }
        // Aggregate numbers first. Resolve/format each distinct full stack once.
        if (symbols != null)
        {
            phase = "symbols";
            var cache = Path.Combine(root, "symcache"); Directory.CreateDirectory(cache);
            symbols.Result.LoadSymbolsAsync(new SymCachePath(cache, []),
                new SymbolPath([Path.GetFullPath(options["--pdb-directory"])]), null, [options["--target-image"]], []).GetAwaiter().GetResult();
            if (files.DirectoryBytes() >= outputMiB * 1024L * 1024) throw new IOException("SYMBOL_CACHE_BUDGET_EXCEEDED");
            files.Json("symbols.json", symbols.Result.Pdbs.Select(p => new { path = p.Path, guid = p.Id, age = p.Age, loaded = p.IsLoaded }));
        }
        phase = "write";
        HeapAggregation.Write(files, snapshots, stacks);
    }
    using var process = Process.GetCurrentProcess();
    bool? captureComplete = null;
    if (options.TryGetValue("--capture-receipt", out var receiptPath))
    {
        using var receipt = JsonDocument.Parse(File.ReadAllText(receiptPath));
        captureComplete = receipt.RootElement.GetProperty("complete").GetBoolean();
    }
    files.Json("analysis-receipt.json", new { analysis_complete = true, attribution_complete = false,
        allocation_attribution = "UNRESOLVED", package_version = "1.12.10",
        elapsed_seconds = watch.Elapsed.TotalSeconds, peak_working_set_bytes = process.PeakWorkingSet64,
        peak_private_commit_bytes = job.PeakPrivateBytes(),
        output_bytes_before_summary = files.DirectoryBytes(), buffered_heap_snapshots = heap != null,
        input_receipt_not_modified = true, capture_complete = captureComplete,
        comparison_eligible = false, scope = "raw ETL aggregation; business boundaries are validated separately",
        symbols_requested = symbols != null });
}
catch (Exception error)
{
    try { files.Json("analysis-failure.json", new { complete = false, phase, failure = error.Message,
        elapsed_seconds = watch.Elapsed.TotalSeconds }); } catch { }
    Console.Error.WriteLine($"{phase}:{error}");
    return 1;
}
return 0;

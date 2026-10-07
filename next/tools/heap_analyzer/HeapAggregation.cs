using Microsoft.Windows.EventTracing;
using Microsoft.Windows.EventTracing.Memory;
using System.Security.Cryptography;
using System.Text;

namespace Wvd.HeapAnalyzer;

internal sealed record StackRecord(string Id, string Canonical, IHeapAllocation Allocation, bool Missing);
internal sealed record HeapTotals(long Blocks, long Bytes);
internal sealed record SnapshotRows(int Index, long TimestampNs, string Utc, bool Is32Bit,
    string ProcessInstance, bool CreationVerified, string? RecordedCreationUtc,
    long Blocks, long Bytes, long UnknownBytes, Dictionary<string, HeapTotals> Stacks)
{
    public string Phase { get; init; } = "unbound";
}

internal static class HeapAggregation
{
    public static List<SnapshotRows> Read(IHeapSnapshotDataSource data, int pid, long creationFiletime, string expectedImage,
        Dictionary<string, StackRecord> stacks)
    {
        var snapshots = new List<SnapshotRows>();
        var traceIds = new Dictionary<(long, bool), string>();
        string? processIdentity = null;
        foreach (var snapshot in data.Snapshots.Where(s => s.ProcessId == pid).OrderBy(s => s.Timestamp.Nanoseconds))
        {
            var process = snapshot.Process ?? throw new InvalidDataException("SNAPSHOT_PROCESS_INSTANCE_MISSING");
            if (!string.Equals(process.ImageName, expectedImage, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("SNAPSHOT_IMAGE_MISMATCH");
            var creation = process.CreateTime;
            var verified = creation.HasValue && creation.Value.HasValue && !creation.Value.IsPartial;
            if (verified && creation!.Value.DateTimeOffset.ToFileTime() != creationFiletime) throw new InvalidDataException("SNAPSHOT_CREATION_TIME_MISMATCH");
            var recorded = creation.HasValue && creation.Value.HasValue ? creation.Value.DateTimeOffset.ToString("O") : null;
            var identity = $"{pid}/{creationFiletime}/{process.ObjectAddress}/{recorded}";
            if (processIdentity != null && processIdentity != identity) throw new InvalidDataException("MULTIPLE_PROCESS_INSTANCES_FOR_PID");
            processIdentity = identity;
            var counts = new Dictionary<string, HeapTotals>();
            long totalBytes = 0, totalBlocks = 0, unknown = 0;
            foreach (var allocation in snapshot.Allocations)
            {
                string id;
                if (allocation.TraceUniqueStackId.HasValue && traceIds.TryGetValue((allocation.TraceUniqueStackId.Value, snapshot.Is32Bit), out var known)) id = known;
                else
                {
                    var canonical = (snapshot.Is32Bit ? "x86|" : "x64|") + Canonical(allocation, out var missing);
                    id = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(canonical)));
                    if (stacks.TryGetValue(id, out var prior) && prior.Canonical != canonical) throw new InvalidDataException("STACK_HASH_COLLISION");
                    stacks.TryAdd(id, new StackRecord(id, canonical, allocation, missing));
                    if (allocation.TraceUniqueStackId.HasValue) traceIds.Add((allocation.TraceUniqueStackId.Value, snapshot.Is32Bit), id);
                }
                var bytes = allocation.Size.Bytes;
                if (bytes < 0) throw new InvalidDataException("NEGATIVE_ALLOCATION_SIZE");
                var old = counts.GetValueOrDefault(id, new HeapTotals(0, 0));
                counts[id] = new HeapTotals(checked(old.Blocks + 1), checked(old.Bytes + bytes));
                totalBlocks = checked(totalBlocks+1); totalBytes = checked(totalBytes + bytes);
                if (stacks[id].Missing) unknown = checked(unknown + bytes);
            }
            if (counts.Values.Sum(v => v.Blocks) != totalBlocks || counts.Values.Sum(v => v.Bytes) != totalBytes)
                throw new InvalidDataException("HEAP_TOTALS_NOT_CONSERVED");
            snapshots.Add(new SnapshotRows(snapshot.Index, snapshot.Timestamp.Nanoseconds,
                snapshot.Timestamp.DateTimeOffset.ToString("O"), snapshot.Is32Bit, identity, verified, recorded,
                totalBlocks, totalBytes, unknown, counts));
        }
        if (snapshots.Count == 0) throw new InvalidDataException("TARGET_HEAP_SNAPSHOTS_MISSING");
        return snapshots;
    }

    private static string Canonical(IHeapAllocation allocation, out bool missing)
    {
        var parts = new StringBuilder();
        missing = allocation.StackInstructionPointers == null || allocation.StackInstructionPointers.Count == 0;
        var frames = allocation.Stack?.Frames;
        if (frames != null && allocation.StackInstructionPointers != null && frames.Count == allocation.StackInstructionPointers.Count)
        {
            foreach (var frame in frames)
            {
                var image = frame.Image;
                if (image != null) parts.Append(image.Path).Append('|').Append(image.Timestamp).Append('|')
                    .Append(image.Checksum).Append('|').Append(image.Size.Bytes).Append('|')
                    .Append(image.Pdb?.Id).Append('|').Append(image.Pdb?.Age).Append('|')
                    .Append(frame.RelativeVirtualAddress).Append(';');
                else parts.Append("unknown@").Append(frame.Address).Append(';');
            }
        }
        else if (allocation.StackInstructionPointers != null) foreach (var address in allocation.StackInstructionPointers) parts.Append("raw@").Append(address).Append(';');
        if (missing) parts.Append("missing@").Append(allocation.AllocationAddress);
        return parts.ToString();
    }

    public static string Text(StackRecord record) => record.Allocation.Stack == null
        ? record.Canonical
        : string.Join(" / ", record.Allocation.Stack.Frames.Select(f =>
            (f.Image?.FileName ?? "unknown") + "!" + (f.Symbol?.FunctionName ?? "<unresolved>") + "+" + f.RelativeVirtualAddress));

    public static void Write(ReportFiles files, List<SnapshotRows> snapshots, Dictionary<string, StackRecord> stacks)
    {
        files.Json("snapshot-map.json", snapshots.Select(s => new { index = s.Index, snapshot_id = $"{s.Index}/{(s.Is32Bit ? 32 : 64)}",
            process_instance = s.ProcessInstance, creation_verified = s.CreationVerified,
            recorded_creation_utc = s.RecordedCreationUtc, phase = s.Phase, timestamp_ns = s.TimestampNs,
            utc = s.Utc, is_32_bit = s.Is32Bit, live_blocks = s.Blocks, live_bytes = s.Bytes,
            unknown_stack_bytes = s.UnknownBytes, unique_stacks = s.Stacks.Count }));
        files.Write("stack-dictionary.jsonl", w => {
            foreach (var record in stacks.Values.OrderBy(s => s.Id))
                w.WriteLine(System.Text.Json.JsonSerializer.Serialize(new { stack_key = record.Id,
                    canonical_address_identity = record.Canonical, full_stack = Text(record), missing_stack = record.Missing }));
        });
        files.Write("heap-outstanding-by-stack.csv", w => {
            w.WriteLine("process_instance,snapshot_id,phase,timestamp,stack_key,live_blocks,live_bytes");
            foreach (var s in snapshots) foreach (var row in s.Stacks.OrderByDescending(v => v.Value.Bytes))
                w.WriteLine($"{ReportFiles.Csv(s.ProcessInstance)},{s.Index}/{(s.Is32Bit?32:64)},{ReportFiles.Csv(s.Phase)},{ReportFiles.Csv(s.Utc)},{row.Key},{row.Value.Blocks},{row.Value.Bytes}");
        });
        files.Write("heap-delta-by-stack.csv", w => {
            w.WriteLine("from_snapshot,to_snapshot,phase,stack_key,before_blocks,after_blocks,delta_blocks,before_bytes,after_bytes,delta_bytes");
            for (var i = 1; i < snapshots.Count; i++)
            {
                var before = snapshots[i - 1]; var after = snapshots[i];
                var phase = ReportFiles.Csv(before.Phase + "->" + after.Phase);
                long byteDelta = 0, blockDelta = 0;
                foreach (var key in before.Stacks.Keys.Union(after.Stacks.Keys).Order())
                {
                    var a = before.Stacks.GetValueOrDefault(key, new HeapTotals(0, 0));
                    var b = after.Stacks.GetValueOrDefault(key, new HeapTotals(0, 0));
                    var deltaBytes = checked(b.Bytes-a.Bytes); var deltaBlocks = checked(b.Blocks-a.Blocks);
                    byteDelta = checked(byteDelta + deltaBytes); blockDelta = checked(blockDelta + deltaBlocks);
                    w.WriteLine($"{before.Index}/{(before.Is32Bit?32:64)},{after.Index}/{(after.Is32Bit?32:64)},{phase},{key},{a.Blocks},{b.Blocks},{deltaBlocks},{a.Bytes},{b.Bytes},{deltaBytes}");
                }
                if (byteDelta != after.Bytes - before.Bytes || blockDelta != after.Blocks - before.Blocks)
                    throw new InvalidDataException("HEAP_DIFF_NOT_CONSERVED");
            }
        });
        files.Write("missing-symbol-coverage.csv", w => {
            w.WriteLine("snapshot_id,module,covered_live_bytes,covered_live_blocks,byte_fraction,non_additive");
            foreach (var snapshot in snapshots)
            {
                var modules = new Dictionary<string, HeapTotals>();
                foreach (var row in snapshot.Stacks)
                {
                    var names = stacks[row.Key].Allocation.Stack?.Frames.Where(f => f.Symbol == null)
                        .Select(f => f.Image?.FileName ?? "unknown").Distinct() ?? ["unknown"];
                    foreach (var name in names)
                    {
                        var prior = modules.GetValueOrDefault(name, new HeapTotals(0, 0));
                        modules[name] = new HeapTotals(checked(prior.Blocks+row.Value.Blocks), checked(prior.Bytes+row.Value.Bytes));
                    }
                }
                foreach (var row in modules.OrderByDescending(v => v.Value.Bytes))
                    w.WriteLine($"{snapshot.Index}/{(snapshot.Is32Bit?32:64)},{ReportFiles.Csv(row.Key)},{row.Value.Bytes},{row.Value.Blocks},{(snapshot.Bytes==0?0:(double)row.Value.Bytes/snapshot.Bytes).ToString(System.Globalization.CultureInfo.InvariantCulture)},true");
            }
        });
    }
}

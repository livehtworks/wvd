# Heap Snapshot Analysis

Developer-only x64 Windows tool. It does not link into automationd or control devices.
Microsoft TraceProcessor 1.12.10 is pinned by `packages.lock.json`. HeapSnapshots is a
buffered SDK data source, not a streaming or constant-memory implementation.

```powershell
dotnet restore next/tools/heap_analyzer/HeapAnalyzer.csproj --locked-mode --source https://api.nuget.org/v3/index.json
dotnet build next/tools/heap_analyzer/HeapAnalyzer.csproj --no-restore -c Release
next/tools/export_memory_stacks.ps1 -EvidenceRoot <evidence> -PdbPath <matching-pdb>
```

Use `-Mode inspect-incomplete` explicitly for a historical incomplete capture.
The input receipt, profiles and ETL remain unchanged. The wrapper records the
input hash, EXE/PDB GUID/Age/hash, tool hashes, SDK version and budget before work.
The analyzer checks one process instance and pointer width and binds each snapshot
by its actual timestamp, not by its ordinal position in a list. Missing SDK create
time remains unknown; only actual OS before/after capture evidence can supplement it.

Defaults: 120 seconds, 1024 MiB private commit, 128 MiB output, system commit below
98%. Windows Job Object enforces the analyzer commit limit before ETL reading;
the parent observes process memory/system pressure and terminates its own child on
error, cancellation or timeout. Managed files use a UTF-8 byte-bounded writer and
publish their final names only after successful close. Native symbol-cache writes
are additionally watched by the parent; that directory watcher is not a disk quota.
Budget errors preserve `.partial` files and failed receipts, never widen limits.

Outputs:

- `heap-outstanding-by-stack.csv`: complete leaf allocation totals per snapshot/stack.
- `heap-delta-by-stack.csv`: signed, conserved changes, including negative and zero rows.
- `stack-dictionary.jsonl`: full address identity and one text rendering per distinct stack.
- `snapshot-map.json`: process instance, pointer width, actual timestamp and capture phase.
- `trace-integrity.json`: final SDK ETL completion/loss statistics.
- `missing-symbol-coverage.csv`: per-module byte/block coverage; rows are non-additive.
- `analysis-receipt.json`: analysis completion, separate capture/comparison/attribution status.

The current wrapper analyzes heap snapshots only. Recorded VirtualAlloc events are
not interpreted or added to heap bytes. A successful file parse is not a successful
business comparison, and a stack delta does not prove continuous lifetime of an
individual address. Main comparisons require two verified `batch_payloads_released`
endpoints; background, partial and legacy unverified boundaries cannot authorize them.

Focused checks use isolated roots under `next/.local/`: `test_event_page_copy`,
`heap_fixture`, `verify_heap_capture.ps1`, `verify_trace_guards.ps1`, and the
`analyzer_budget` project. Capture of the fixture requires administrator rights;
it never enables snapshots for the game/tool process. No full game loop is needed.

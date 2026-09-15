# Explicit Audit Delivery

## Scope

These standard-library tools read source and explicitly named evidence. They never
build, run an EXE, discover devices, import old application modules, commit, push,
or upload. A review with failures or unverified historical identity is valid;
unsafe paths, leaks, changed inputs, and hash mismatches are not.

The tools are `next/tools/audit_snapshot.py` and `audit_package.py`. Run with the
repository Python and `-B`. Paths in the spec and command arguments are relative
to the repository unless explicitly described otherwise. All output goes to new
paths under `next/.local`. Existing output is never overwritten.

`source` walks only the `next` source tree, pruning `.local`, `.vscode`, `build`,
`dist`, `node_modules`, `webnode_modules`, `__pycache__`, `mod`, SDK/model and Git
directories. **Evidence is never walked**: each file must have its own entry.
No globs, directory evidence inputs, or paths embedded in results are followed.

## Private Spec

`--spec` accepts either a private UTF-8 JSON file under `next/.local` or `-` for
stdin. `repo` must be the actual absolute repository root. Do not publish the spec:
it contains path mappings and may contain literal secret values.

The following is the field contract, not a final evidence manifest. Replace the
example case entries with the complete, explicit intended case denominator.

```json
{
  "schema": 1,
  "repo": "<absolute repository root, private only>",
  "baseline": "4d7c4fabd29bfadf55963bfdaecf79e3b6582bdd",
  "source_snapshot": "next/.local/review-final/source.json",
  "generated_build_id": "next/build/generated/core_build_id.hpp",
  "artifacts": [
    {"id": "workflow", "path": "next/build/m4/Release/test_m4_workflow.exe"}
  ],
  "artifact_snapshot": "next/.local/review-artifacts/artifacts.json",
  "path_aliases": {},
  "secret_values": [],
  "evidence": [
    {"id": "batch-log", "path": "next/.local/logs/completed-batch.log", "member": "batch/log.txt", "format": "text"},
    {"id": "case-execution", "path": "next/.local/completed-root/case/execution.json", "member": "batch/case/execution.json", "format": "json"},
    {"id": "case-result", "path": "next/.local/completed-root/case/output.json", "member": "batch/case/output.json", "format": "json"}
  ],
  "cases": [
    {"id": "batch/case", "batch": "batch", "reported_status": "FAIL", "report": "batch-log", "execution": "case-execution", "result": "case-result"},
    {"id": "batch/not-run", "reported_status": "NOT_RUN"}
  ],
  "protection": {
    "before": "next/.local/work/protection-before.json",
    "after": "next/.local/work/protection-after-summary.json"
  },
  "commands": [
    {"id": "batch", "argv": ["<PYTHON>", "-B", "-m", "unittest", "example.module.Test.test_case", "-v"], "cwd": "<REPO>", "exit": 1, "watchdog_seconds": null, "evidence": "batch-log", "provenance": "recorded by batch owner"}
  ]
}
```

- Enumerate **all current EXEs intended for the final review** in `artifacts`.
  Each is read for identity only; no executable bytes enter the ZIP. No EXE list
  is inferred from directory contents. The example lists only one, not the final set.
- `evidence[].format` is `json`, `jsonl`, or `text`; `member` is the public relative
  filename under `evidence/`. Optional `sha256` pins the original file bytes.
  Repeated IDs/member names and Windows case collisions are rejected.
- Include persisted RunStore results, SDK module records, dependency/model lock
  metadata, historical build identities, structured costs, and failure records as
  separate explicit evidence files. Model/SDK binaries are never exported.
- Missing/invalid-JSON evidence becomes an UNVERIFIED record with an original hash
  when available. Invalid original bytes are not exported as supposedly safe text.
- `reported_status` is the batch owner's report, not an exporter test judgment.
  `cases.json` separately records exit, observed state, counts, quiescent, mismatch,
  and artifact hash matches. A negative test's Failed state is not a successful
  business run; exit zero does not generate PASS. Null remains null.
- Commands are existing recorded argv/cwd/exit/watchdog values, never executed by
  these tools. Unrecorded values must be null/UNVERIFIED, not guessed from current
  test source. Include the command record's supporting evidence separately.
- Protection summaries are derived **only from the named records**. The exporter
  does not open the 450 protected files, config, mod, or any user data. Existing
  `changed`/`production_writes` claims retain their reported provenance; no current
  protection remeasurement is implied.

## Source And Build Identity

`source` uses read-only Git operations (`ls-tree`, `cat-file --batch`, `ls-files`)
against fixed base `4d7c4fa`. No checkout/index changes occur. The source snapshot
contains the **complete current accepted text-source inventory**, not just a diff:
raw SHA-256, bytes, baseline SHA-256, tracked/untracked status and byte-level change.
Deleted baseline sources are recorded. CRLF differences are real byte differences
here, not a claim of semantic code changes. Unclassified files are listed explicitly.
Non-source images/binaries and private config/input files are excluded.

The payload contains added/modified text sources plus lock/build descriptions;
unchanged files remain in the full inventory and can be obtained from the fixed
base. No raw Git patch containing unsanitized strings is exported. `delta_sha256`
identifies the canonical change/deletion manifest, not an executable patch.

The CMake core ID is computed independently using the current rule in
`next/native/maafw/CMakeLists.txt`: native and tests/native `.cpp/.hpp`, that CMake
file, dependencies.lock.json and optional opencv.lock.json. Python, web and docs
are covered by the full inventory, **not by that narrower core ID**. A future CMake
identity-rule change requires reviewing the small `core_id` function too.

`artifacts` records EXE hashes and the exact generated header hash/build_id. It
reports whether full source identity stayed unchanged since the before snapshot,
and whether current core sources match the header. **Neither proves EXE/source
binding by itself.** The default `source_binding` is UNVERIFIED.

An optional private `build_record` spec path can refer to an existing build-owner
record with `source_inventory_sha256`, `exit`, `build_id`, and `exe_sha256` (map of
artifact ID to SHA-256). Only matching values produce `RECORDED_BUILD_MATCH`, not
cryptographic proof that this tool ran the build. Include that original build record,
actual commands and build log in the explicit evidence list. Never manufacture a
successful receipt from the newly observed hashes to upgrade an old build.

Historical results retain their own execution hash. Matching a current EXE produces
only `EXE_HASH_MATCH`; historical source binding remains UNVERIFIED in the case
summary. Existing stronger historical records remain available as evidence, never
reassigned to the newest source snapshot. Failure/UNVERIFIED records do not block ZIP.

## Redaction And Safety

- Read roots are controlled: source under `next`, evidence under `next/.local`,
  current artifact/header identities under `next/build`. Symlinks and Windows
  reparse points are rejected. No user-home/SDK path from a JSON value is followed.
- Add explicit private absolute-prefix mappings in `path_aliases`, with placeholders
  such as `<SDK>`, `<USER_HOME>` or `<TOOLS>`. `<REPO>` is automatic. Longest prefixes
  go first; slash, backslash and escaped-backslash forms are supported.
- JSON/JSONL is parsed structurally. Profile/config/environment/export/values/private
  input containers and recognized credential/device fields are replaced wholesale.
  Unknown fields inside a removed profile cannot leak. Other fields retain meaning.
- Literal secrets can be listed in private `secret_values` (minimum four characters).
  Recognized secret assignments/command options are redacted. Credential-like tokens,
  private keys, unmapped drive/UNC/common local Unix paths or credential URLs block
  export. A Python-repr profile in a text log blocks export: supply a separately
  reviewed structured summary instead of weakening the filter.
- Text source redaction may change string literals; raw and redacted hashes are
  deliberately different identities. A recipient needs appropriate local placeholder
  bindings to reproduce commands. The ZIP is not claimed byte-identical to private
  build inputs. No pattern-based filter can certify arbitrary unknown secrets;
  the explicit list owner must review free-form source/log text and map additional
  private values. Do not whitelist a whole log/root to silence a leak finding.
- No EXE/DLL/SDK/model/image/config/mod/input original is a payload. Only text-source
  files and explicit safe evidence formats are admitted. A mismatched pinned hash,
  changed current EXE/header or mutation during capture/pack is an error.

## Freeze Sequence

Do not run `pack` while any source owner is still editing or native evidence is
still being produced. The commands below are for the main agent's next controlled
freeze, not an authorization to build or package during tool development.

1. Prepare all generated source/data using existing authorized procedures first.
   Freeze source owners, then capture complete pre-build identity:

```powershell
.venv-build/Scripts/python.exe -B next/tools/audit_snapshot.py source --spec next/.local/review-spec.json --out next/.local/review-before
```

2. The main agent runs its separately authorized build, recording actual argv,
   exit, log and any generated-source changes. These tools do not run that build.
   Capture post-build source and artifacts without editing source in between:

```powershell
.venv-build/Scripts/python.exe -B next/tools/audit_snapshot.py source --spec next/.local/review-spec.json --out next/.local/review-after
.venv-build/Scripts/python.exe -B next/tools/audit_snapshot.py artifacts --spec next/.local/review-spec.json --source next/.local/review-before/source.json --out next/.local/review-artifacts
```

3. Compare before/after `inventory_sha256`, artifact/header observations and actual
   build records. Any generated-source difference is reported, not retroactively
   folded into the before identity. Run tests only under the main agent's lease.
   Enumerate the completed evidence files and actual method denominator in the spec.
   Once all source/docs are finally frozen, capture `review-final/source.json` using
   the same `source` command with that new output directory.

4. Set `source_snapshot` and `artifact_snapshot` in the private spec to the intended
   final records, then check. Failure and unverified historical evidence may remain:

```powershell
.venv-build/Scripts/python.exe -B next/tools/audit_package.py check --spec next/.local/review-spec.json
.venv-build/Scripts/python.exe -B next/tools/audit_package.py pack --spec next/.local/review-spec.json --freeze next/.local/review-final/source.json --out next/.local/review-delivery/review.zip
.venv-build/Scripts/python.exe -B next/tools/audit_package.py verify --archive next/.local/review-delivery/review.zip --receipt next/.local/review-delivery/review.receipt.json
```

`pack` rechecks the complete source inventory and every read input before issuing
a success receipt. ZIP timestamps/order are deterministic. `INDEX.json` covers all
payload members with original and redacted hashes; the external receipt covers the
index and whole ZIP without a circular self-hash. `verify` reads members in place,
does not extract, and rejects missing/extra/duplicate/unsafe members or wrong hashes.
If interrupted or mutation is detected after ZIP creation, a ZIP without a verified
receipt is incomplete and must not be delivered. Use new output names on retry.

CLI success exits 0; unsafe/invalid input exits 2 with a generic error that does not
echo private values. `check` success means packaging checks passed, not tests passed.

## Isolated Tests

```powershell
.venv-build/Scripts/python.exe -B -m unittest discover -s next/tests -p test_audit_delivery.py -v
```

All test data and test ZIPs are created in disposable `TemporaryDirectory` fixtures.
Fake EXE files are inert byte strings, only hashed. No Git or native process runs in
these tests. The only mocks isolate stdin and platform link metadata; source capture,
redaction, evidence collection, packing and verification run their real code paths.
Tests of the Git adapter and an actual final review export require a later explicit
freeze; the unit suite is not evidence that a final review package was generated.

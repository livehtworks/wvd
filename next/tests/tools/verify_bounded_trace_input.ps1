param([Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $root) { throw 'NEW_ISOLATED_ROOT_REQUIRED' }
[IO.Directory]::CreateDirectory($root) | Out-Null
Add-Type -Path (Join-Path $PSScriptRoot '../../tools/bounded_trace_input.cs')
$file = Join-Path $root 'rows.jsonl'
$utf8 = [Text.UTF8Encoding]::new($false)
function Assert-Rejected([string]$Expected) {
    try { @([WvdBoundedTraceInput]::Lines($file, 64, 2, 8)) | Out-Null }
    catch { if ($_.Exception.ToString().Contains($Expected)) { return }; throw }
    throw "EXPECTED_REJECTION:$Expected"
}
[IO.File]::WriteAllText($file, "one`ntwo`n", $utf8)
$rows = @([WvdBoundedTraceInput]::Lines($file, 64, 2, 8))
if ($rows.Count -ne 2 -or $rows[1] -ne 'two') { throw 'COMPLETE_ROWS_CHANGED' }
[IO.File]::WriteAllText($file, 'partial', $utf8)
Assert-Rejected 'TRACE_EVENT_PARTIAL_LINE'
[IO.File]::WriteAllText($file, "123456789`n", $utf8)
Assert-Rejected 'TRACE_EVENT_LINE_EXCEEDED'
[IO.File]::WriteAllText($file, "1`n2`n3`n", $utf8)
Assert-Rejected 'TRACE_EVENT_RECORDS_EXCEEDED'
[IO.File]::WriteAllText($file, ('x' * 65), $utf8)
Assert-Rejected 'TRACE_EVENT_BYTES_EXCEEDED'
[IO.File]::WriteAllBytes($file, [byte[]](0xc3, 0x28, 0x0a))
Assert-Rejected 'DecoderFallbackException'
Write-Output 'PASS bounded history: complete rows, partial tail, line/record/file caps, invalid UTF8'

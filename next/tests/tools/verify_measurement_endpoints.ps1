#requires -Version 7.0
param([Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(Test-Path -LiteralPath $EvidenceRoot){throw 'NEW_ISOLATED_ROOT_REQUIRED'}
[IO.Directory]::CreateDirectory($EvidenceRoot)|Out-Null
Import-Module (Join-Path $PSScriptRoot '../../tools/measurement_endpoint.psm1') -Force
$target=[pscustomobject]@{pid=123;process_start_filetime='456';instance_id='owned-server'}
$first=[pscustomobject]@{schema_version=1;server_instance_id='owned-server';process_id=123;process_created_100ns=456;
    run_id=1;round=8;generation=2;phase='worker_joined';release_scope='worker';input_clean=$true;
    cleanup_complete=$true;heap_maintenance_complete=$true;configuration=[pscustomobject]@{scope='PID'};
    monotonic_ns=100;controller_id='isolated-collector';sequence=1}
$second=$first|ConvertTo-Json -Depth 8|ConvertFrom-Json
$second.run_id=2;$second.round=9;$second.sequence=2;$second.monotonic_ns=200
Assert-MeasurementPair $first $second $target
$rows=[Collections.Generic.List[object]]::new()
$rows.Add(@{case='two_real_contract_joined_endpoints_eligible';passed=$true})
foreach($fault in @('pid','creation','server','phase','input','round','configuration','sequence')) {
    $changed=$second|ConvertTo-Json -Depth 8|ConvertFrom-Json
    switch($fault) {
        pid {$changed.process_id=999}
        creation {$changed.process_created_100ns=999}
        server {$changed.server_instance_id='foreign'}
        phase {$changed.phase='batch_payloads_released';$changed.release_scope='batch'}
        input {$changed.input_clean=$false}
        round {$changed.round=11}
        configuration {$changed.configuration.scope='system'}
        sequence {$changed.sequence=7}
    }
    $failure=''
    try {Assert-MeasurementPair $first $changed $target} catch {$failure=$_.Exception.Message}
    if(-not $failure){throw "INVALID_MEASUREMENT_PAIR_ACCEPTED:$fault"}
    $rows.Add(@{case=$fault;passed=$true;failure=$failure})
}
$rows|ConvertTo-Json -Depth 5|Set-Content -Encoding utf8 (Join-Path $EvidenceRoot 'results.json')
Write-Output ('Passed shared collector/export endpoint contract cases: '+$rows.Count)

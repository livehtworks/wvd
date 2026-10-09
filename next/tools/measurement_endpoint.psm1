Set-StrictMode -Version Latest
function Assert-MeasurementEndpoint {
    param($Endpoint,$Target)
    foreach($field in @('schema_version','server_instance_id','process_id','process_created_100ns','run_id',
        'round','generation','phase','release_scope','input_clean','cleanup_complete','heap_maintenance_complete',
        'configuration','monotonic_ns','controller_id','sequence')) {
        if(-not $Endpoint.PSObject.Properties[$field]){throw "MEASUREMENT_FIELD_MISSING:$field"}
    }
    if($Endpoint.schema_version -ne 1 -or $Endpoint.phase -ne 'worker_joined' -or
        $Endpoint.release_scope -ne 'worker' -or -not $Endpoint.input_clean -or
        -not $Endpoint.cleanup_complete -or -not $Endpoint.heap_maintenance_complete -or
        $Endpoint.monotonic_ns -le 0 -or $Endpoint.run_id -le 0 -or $Endpoint.generation -le 0 -or
        $Endpoint.round -le 0 -or $Endpoint.sequence -le 0){throw 'MEASUREMENT_ENDPOINT_NOT_JOINED_CLEAN'}
    if($Endpoint.process_id -ne $Target.pid -or
        [string]$Endpoint.process_created_100ns -ne [string]$Target.process_start_filetime -or
        $Endpoint.server_instance_id -ne $Target.instance_id){throw 'MEASUREMENT_PROCESS_IDENTITY_MISMATCH'}
}
function Assert-MeasurementPair {
    param($First,$Second,$Target)
    Assert-MeasurementEndpoint $First $Target
    Assert-MeasurementEndpoint $Second $Target
    if($First.controller_id -ne $Second.controller_id -or $First.phase -ne $Second.phase -or
        $First.release_scope -ne $Second.release_scope -or $First.sequence+1 -ne $Second.sequence -or
        $First.round+1 -ne $Second.round -or $First.run_id -eq $Second.run_id -or
        $First.monotonic_ns -ge $Second.monotonic_ns -or
        ($First.configuration|ConvertTo-Json -Depth 8 -Compress) -ne
        ($Second.configuration|ConvertTo-Json -Depth 8 -Compress)) {throw 'MEASUREMENT_PAIR_SCOPE_MISMATCH'}
}
Export-ModuleMember -Function Assert-MeasurementEndpoint,Assert-MeasurementPair

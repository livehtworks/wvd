param(
    [string]$BaseUrl = 'http://127.0.0.1:17652',
    [int]$PollSeconds = 20
)

$ErrorActionPreference = 'Stop'
$root = Join-Path $env:LOCALAPPDATA 'WvdNext\overnight'
New-Item -ItemType Directory -Path $root -Force | Out-Null
$session = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $root "scorpion-$session.log"
$stopFile = Join-Path $root 'STOP'

function Write-RunLog([string]$message) {
    $line = "$(Get-Date -Format o) $message"
    Add-Content -LiteralPath $log -Encoding UTF8 -Value $line
}

function Read-Run {
    return Invoke-RestMethod -Uri "$BaseUrl/api/v1/runs/current" -TimeoutSec 10
}

function Save-FailureFrame([string]$reason) {
    try {
        Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/v1/device/capture" -TimeoutSec 10 | Out-Null
        for ($i = 0; $i -lt 12; $i++) {
            Start-Sleep -Seconds 2
            $status = Invoke-RestMethod -Uri "$BaseUrl/api/v1/device" -TimeoutSec 10
            if ($status.operation.state -eq 'failed') { break }
            if ($status.operation.state -eq 'completed' -and $status.frame_available -and
                $status.frame.age_ms -lt 10000) {
                $path = Join-Path $root "failure-$session.png"
                Invoke-WebRequest -Uri "$BaseUrl/api/v1/device/frame" -OutFile $path -TimeoutSec 10
                Write-RunLog "failure_frame=$path reason=$reason"
                return
            }
        }
        Write-RunLog "failure_frame_unavailable reason=$reason"
    } catch {
        Write-RunLog "failure_frame_error=$($_.Exception.Message)"
    }
}

try {
    $previous = Read-Run
    if ($previous.busy -or !$previous.quiescent) { throw 'RUN_ALREADY_ACTIVE_OR_NOT_QUIESCENT' }
    Write-RunLog "started previous_run=$($previous.run_id) history=$env:LOCALAPPDATA\WvdNext\recent-frames"
    $cycle = 0
    while (!(Test-Path -LiteralPath $stopFile)) {
        $requestId = [guid]::NewGuid().ToString()
        $body = @{ task_id='Scorpionesses'; request_id=$requestId; resource_locale='zh-Hant' } | ConvertTo-Json -Compress
        $accepted = Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/v1/runs/start" -ContentType 'application/json' -Body $body -TimeoutSec 30
        if (!$accepted.accepted) { throw "START_NOT_ACCEPTED request=$requestId" }
        Write-RunLog "cycle_start request=$requestId"
        $startDeadline = (Get-Date).AddMinutes(2)
        do {
            Start-Sleep -Seconds 2
            $current = Read-Run
            if ($current.submission.request_id -eq $requestId -and
                $current.submission.state -eq 'failed') {
                throw "START_PREPARATION_FAILED request=$requestId error=$($current.submission.error)"
            }
            if ($current.run_id -ne $previous.run_id) { break }
        } while ((Get-Date) -lt $startDeadline)
        if ($current.run_id -eq $previous.run_id) { throw "RUN_ID_NOT_ADVANCED request=$requestId" }
        while ($current.busy -and !(Test-Path -LiteralPath $stopFile)) {
            Start-Sleep -Seconds $PollSeconds
            $current = Read-Run
        }
        if (Test-Path -LiteralPath $stopFile) {
            Write-RunLog "operator_stop_requested run=$($current.run_id); no_new_cycle"
            break
        }
        $business = $current.business.bounty_cycle
        Write-RunLog "cycle_end run=$($current.run_id) state=$($current.state) reason=$($current.reason) units=$($current.completed_business_units) cycles=$($business.completed_cycles) reports=$($business.reports_remaining) rejected=$($current.inputs.rejected) saved=$($current.result_saved) dir=$($current.run_directory)"
        if ($current.state -ne 'Completed' -or !$current.result_saved -or !$current.quiescent -or
            $current.completed_business_units -ne 3 -or $business.completed_cycles -ne 1 -or
            $business.reports_remaining -ne 0 -or $current.inputs.rejected -ne 0 -or
            $current.secondary_errors.Count -gt 0) {
            Save-FailureFrame "run=$($current.run_id) state=$($current.state)"
            throw "CYCLE_NOT_CLEAN run=$($current.run_id) state=$($current.state) reason=$($current.reason)"
        }
        $cycle++
        $previous = $current
        Write-RunLog "clean_cycles=$cycle"
        Start-Sleep -Seconds 10
    }
    Write-RunLog "stopped clean_cycles=$cycle"
} catch {
    Write-RunLog "STOPPED_ON_ERROR $($_.Exception.Message)"
    exit 1
}

[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('start','stop','restart','status')][string]$Action,
    [Parameter(Mandatory)][string]$Binary,
    [Parameter(Mandatory)][string]$Config
)
$ErrorActionPreference = 'Stop'
$binaryPath = (Resolve-Path -LiteralPath $Binary).Path
$configPath = (Resolve-Path -LiteralPath $Config).Path
$settings = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$runtimePath = (Resolve-Path -LiteralPath $settings.runtime_dir).Path
$recordPath = Join-Path $runtimePath 'service-process.json'

function Get-OwnedProcess {
    if (-not (Test-Path -LiteralPath $recordPath)) { return $null }
    $record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
    $process = Get-Process -Id $record.process_id -ErrorAction SilentlyContinue
    if (-not $process) { return $null }
    if ($process.Path -ne $binaryPath -or
        $process.StartTime.ToUniversalTime().Ticks -ne [long]$record.started_at_ticks) {
        throw 'Lifecycle identity mismatch: refusing to control an unrelated process.'
    }
    return $process
}

$owned = Get-OwnedProcess
if ($Action -in @('stop','restart') -and $owned) {
    # Windows service wrappers terminate the owned process; every active worker
    # is contained by CBM's existing Job Object and cannot survive owner exit.
    Stop-Process -Id $owned.Id
    $owned.WaitForExit(10000) | Out-Null
    if (-not $owned.HasExited) { throw 'Owned process did not stop within 10 seconds.' }
    $owned = $null
}
if ($Action -in @('start','restart') -and -not $owned) {
    if ($configPath.Contains('"')) { throw 'Invalid configuration path.' }
    $owned = Start-Process -FilePath $binaryPath -ArgumentList @('asms-mcp-http', ('"' + $configPath + '"')) `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $runtimePath 'service.stdout') `
        -RedirectStandardError (Join-Path $runtimePath 'service.stderr')
    # Decimal ticks stay a string through both PS 5.1 and PS 7 JSON readers.
    # ISO timestamps may be auto-converted to DateTime during deserialization.
    $record = @{ process_id=$owned.Id; started_at_ticks=$owned.StartTime.ToUniversalTime().Ticks.ToString([Globalization.CultureInfo]::InvariantCulture) }
    $record | ConvertTo-Json | Set-Content -LiteralPath $recordPath -Encoding utf8
    Start-Sleep -Milliseconds 200
    $owned.Refresh()
    if ($owned.HasExited) { throw (Get-Content -LiteralPath (Join-Path $runtimePath 'service.stderr') -Raw) }
}
@{ contract='asms-cbm-service/v1'; state=$(if ($owned) {'RUNNING'} else {'STOPPED'}); project_scope=$settings.project_scope } | ConvertTo-Json

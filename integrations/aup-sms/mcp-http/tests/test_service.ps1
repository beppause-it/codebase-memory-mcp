[CmdletBinding()]
param([Parameter(Mandatory)][string]$Binary)
$ErrorActionPreference = 'Stop'
$binaryPath = (Resolve-Path -LiteralPath $Binary).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$testRoot = if ($env:CBM_HTTP_TEST_ROOT) { $env:CBM_HTTP_TEST_ROOT } else { Join-Path $repo 'build' }
$runtime = Join-Path $testRoot ('cr27-service-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runtime | Out-Null
$configPath = Join-Path $runtime 'config.json'
$probe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$probe.Start()
$port = $probe.LocalEndpoint.Port
$probe.Stop()
function Get-SHA256([string]$Text) {
    [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($Text))).ToLowerInvariant()
}
$data = 'asms1.tenant-service.project-service.data.' + [Guid]::NewGuid().ToString('N') + [Guid]::NewGuid().ToString('N')
$control = 'asms1.tenant-service.project-service.control.' + [Guid]::NewGuid().ToString('N') + [Guid]::NewGuid().ToString('N')
@{
    tenant_id='tenant-service'; project_scope='project-service'; port=$port; runtime_dir=$runtime
    data_token_sha256=(Get-SHA256 $data); control_token_sha256=(Get-SHA256 $control)
    attestation_key=([Guid]::NewGuid().ToString('N') + [Guid]::NewGuid().ToString('N')); indexes=@()
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $configPath -Encoding utf8
$service = Join-Path $PSScriptRoot '../service.ps1'
try {
    foreach ($action in @('start','status','restart','status','stop','status')) {
        $state = (& $service -Action $action -Binary $binaryPath -Config $configPath) | ConvertFrom-Json
        $expected = if ($action -in @('stop') -or ($action -eq 'status' -and $stopped)) {'STOPPED'} else {'RUNNING'}
        if ($state.state -ne $expected) { throw "Lifecycle $action returned $($state.state), expected $expected" }
        if ($action -eq 'stop') { $stopped = $true }
        if ($action -in @('start','restart')) {
            $ready = $false
            for ($attempt = 0; $attempt -lt 50; $attempt++) {
                try {
                    $health = Invoke-RestMethod -Uri "http://127.0.0.1:$port/integrations/aup-sms/v1/projects/project-service/health" -Headers @{ Authorization="Bearer $control" } -TimeoutSec 5
                    if ($health.details.state -eq 'ACTIVE') { $ready = $true; break }
                } catch { if ($attempt -eq 49) { throw } }
                Start-Sleep -Milliseconds 100
            }
            if (-not $ready) { throw 'Machine health did not become ACTIVE' }
        }
    }
    'Windows lifecycle start/status/restart/stop and machine health: PASS'
} finally {
    & $service -Action stop -Binary $binaryPath -Config $configPath | Out-Null
}

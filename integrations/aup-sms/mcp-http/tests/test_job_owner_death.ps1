[CmdletBinding()]
param([Parameter(Mandatory)][string]$Binary)
$ErrorActionPreference = 'Stop'
$binaryPath = (Resolve-Path -LiteralPath $Binary).Path
$testRoot = Join-Path $env:LOCALAPPDATA 'CBM-CR27-tests'
$runtime = Join-Path $testRoot ('job-owner-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
$marker = Join-Path $runtime 'created.txt'
$parent = $null
$child = $null
try {
    $parent = Start-Process -FilePath $binaryPath -ArgumentList @('parent', ('"' + $marker + '"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $runtime 'stdout') -RedirectStandardError (Join-Path $runtime 'stderr')
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (-not (Test-Path -LiteralPath $marker) -or (Get-Item -LiteralPath $marker).Length -eq 0) {
        $parent.Refresh()
        if ($parent.HasExited) { throw "Parent exited before the post-create hook: $($parent.ExitCode)" }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Post-create hook deadline exceeded' }
        Start-Sleep -Milliseconds 20
    }
    $created = (Get-Content -LiteralPath $marker -Raw).Trim() -split ' '
    $child = Get-Process -Id ([int]$created[0]) -ErrorAction Stop
    if ($created[1] -ne '1') { throw 'Suspended child was not in the exact owned Job at creation' }
    if ($child.HasExited) { throw 'Child exited before parent-death injection' }
    Stop-Process -Id $parent.Id -Force
    if (-not $parent.WaitForExit(5000)) { throw 'Parent did not terminate' }
    if (-not $child.WaitForExit(5000)) { throw 'Suspended child survived owner death' }
    'Atomic Job membership before ResumeThread and child termination on owner death: PASS'
} finally {
    foreach ($owned in @($parent, $child)) {
        if ($owned -and -not $owned.HasExited) { Stop-Process -Id $owned.Id -Force -ErrorAction SilentlyContinue }
    }
}

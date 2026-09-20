[CmdletBinding()]
param([ValidateRange(1,65535)][int]$Port = 8766, [switch]$NoBrowser)
$ErrorActionPreference = 'Stop'
$creatorScript = Join-Path $PSScriptRoot 'mirror_creator.py'
$creatorPython = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'runtime\python.exe'
if (-not (Test-Path -LiteralPath $creatorPython -PathType Leaf)) {
    $creatorPython = (Get-Command python -ErrorAction Stop).Source
}
$creatorUrl = "http://127.0.0.1:$Port"
$creatorReady = $false
try {
    $creatorHealth = Invoke-RestMethod -Uri "$creatorUrl/api/health" -TimeoutSec 2
    $creatorReady = $creatorHealth.app -eq 'Mirror Creator for Fallout 4'
} catch { }
if ($creatorReady -and ($creatorHealth.version -ne '1.0' -or $creatorHealth.game -ne 'fallout4' -or $creatorHealth.multiAreaSelection -ne $true -or $creatorHealth.ckPartIdentification -ne $true)) {
    throw "A different Mirror Creator version is using port $Port. Close that preview server, or start this version with a different -Port value."
}
if (-not $creatorReady) {
    # Prefer pythonw so the server has no dependency on the launcher's console.
    $creatorPythonWindowless = Join-Path (Split-Path -Parent $creatorPython) 'pythonw.exe'
    if (Test-Path -LiteralPath $creatorPythonWindowless) { $creatorPython = $creatorPythonWindowless }
    $creatorProcess = Start-Process -FilePath $creatorPython -ArgumentList @('-B', ('"' + $creatorScript + '"'), '--port', $Port) -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru
    for ($creatorAttempt = 0; $creatorAttempt -lt 25; $creatorAttempt++) {
        Start-Sleep -Milliseconds 200
        try {
            $creatorHealth = Invoke-RestMethod -Uri "$creatorUrl/api/health" -TimeoutSec 1
            if ($creatorHealth.app -eq 'Mirror Creator for Fallout 4') { $creatorReady = $true; break }
        } catch { }
        if ($creatorProcess.HasExited) { break }
    }
    if (-not $creatorReady) { throw "Mirror Creator could not start on port $Port. Try a different -Port value or run python mirror_creator.py for details." }
}
if ($creatorHealth.version -ne '1.0' -or $creatorHealth.game -ne 'fallout4' -or $creatorHealth.multiAreaSelection -ne $true -or $creatorHealth.ckPartIdentification -ne $true) {
    throw "Mirror Creator on port $Port does not match this Fallout 4 package. Try a different -Port value."
}
# This browser window is the interactive tool the author needs to use.
if (-not $NoBrowser) { Start-Process "$creatorUrl/" }
Write-Output "Mirror Creator: $creatorUrl/ (all processing stays on this computer)."

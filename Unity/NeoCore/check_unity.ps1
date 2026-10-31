[CmdletBinding()]
param([string]$Unity = 'C:\Program Files\Unity\Hub\Editor\2022.3.62f3\Editor\Unity.exe')

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$projectDir = Join-Path $repoRoot 'Build\neocore-unity-smoke'
$packageDir = Join-Path $repoRoot 'Unity\com.neocore.unity'
$embeddedPackage = Join-Path $projectDir 'Packages\com.neocore.unity'
$logFile = Join-Path $projectDir 'smoke.log'
if (-not (Test-Path -LiteralPath $Unity)) { throw "Unity editor not found: $Unity" }
if (-not (Test-Path -LiteralPath (Join-Path $packageDir 'Runtime\Plugins\x86_64\NeoCoreUnity.dll'))) {
    throw 'Build the typed plugin first: pwsh Unity/NeoCore/build_windows.ps1'
}
New-Item -ItemType Directory -Force -Path $projectDir,$embeddedPackage,(Join-Path $projectDir 'Assets\Basic'),(Join-Path $projectDir 'Assets\Editor'),(Join-Path $projectDir 'ProjectSettings') | Out-Null
Get-ChildItem -LiteralPath $packageDir | Copy-Item -Destination $embeddedPackage -Recurse -Force
Get-ChildItem -LiteralPath (Join-Path $packageDir 'Samples~\Basic') | Copy-Item -Destination (Join-Path $projectDir 'Assets\Basic') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'UnitySmoke.cs') -Destination (Join-Path $projectDir 'Assets\Editor\UnitySmoke.cs') -Force
Set-Content -LiteralPath (Join-Path $projectDir 'Packages\manifest.json') -Value '{"dependencies":{}}' -Encoding utf8

$process = Start-Process -FilePath $Unity -ArgumentList @('-batchmode','-nographics','-projectPath',('"' + $projectDir + '"'),'-executeMethod','NeoCoreUnitySmoke.Run','-logFile',('"' + $logFile + '"')) -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(240000)) {
    Stop-Process -Id $process.Id
    throw "Unity smoke exceeded four minutes; inspect $logFile"
}
if ($process.ExitCode -ne 0) { throw "Unity smoke failed ($($process.ExitCode)); inspect $logFile" }
if (-not (Select-String -LiteralPath $logFile -SimpleMatch 'NEOCORE_UNITY_SMOKE_PASSED' -Quiet)) {
    throw "Unity exited without the completed smoke marker; inspect $logFile"
}
Write-Host "Unity typed sample passed: $logFile"

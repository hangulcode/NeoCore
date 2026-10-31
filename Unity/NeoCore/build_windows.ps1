[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$Generator = '',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildDir = Join-Path $repoRoot 'Build\neocore-unity-win64'
$packageDir = Join-Path $repoRoot 'Unity\com.neocore.unity'
$pluginDir = Join-Path $packageDir 'Runtime\Plugins\x86_64'

if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    $resolvedBuild = (Resolve-Path -LiteralPath $buildDir).Path
    $buildRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'Build'))
    if (-not $resolvedBuild.StartsWith($buildRoot + [System.IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to clean a directory outside the repository build output'
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

$configureArgs = @('-S', $repoRoot, '-B', $buildDir, '-A', 'x64', '-DNEOCORE_BUILD_UNITY_BRIDGE=ON', '-DNEOCORE_BUILD_EXAMPLES=OFF', '-DNEOCORE_BUILD_TESTS=ON')
if ($Generator) { $configureArgs += @('-G', $Generator) }
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }
& cmake --build $buildDir --config $Configuration --target NeoCoreUnity neocore_unity_c_smoke --parallel
if ($LASTEXITCODE -ne 0) { throw "CMake build failed ($LASTEXITCODE)" }
& ctest --test-dir $buildDir -C $Configuration -R '^neocore_unity_c_smoke$' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "NeoCore C ABI smoke failed ($LASTEXITCODE)" }

New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
Copy-Item -LiteralPath (Join-Path $buildDir "Unity\NeoCore\$Configuration\NeoCoreUnity.dll") -Destination (Join-Path $pluginDir 'NeoCoreUnity.dll') -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $packageDir -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.txt') -Destination $packageDir -Force
Write-Host "Built and checked typed plugin: $pluginDir\NeoCoreUnity.dll"

# build_android.ps1 — Android 네이티브 플러그인을 빌드해 패키지에 넣는다.
#
#   pwsh Unity\build_android.ps1                 # arm64-v8a
#   pwsh Unity\build_android.ps1 -Abi armeabi-v7a
#
# NDK 는 ANDROID_NDK_ROOT / ANDROID_NDK_HOME / ANDROID_HOME\ndk\* / %LOCALAPPDATA%\Android\Sdk\ndk\*
# 순서로 찾는다. 빌드 도구는 ninja 가 PATH 에 있으면 그걸 쓰고, 없으면 NDK 가 같이 배포하는
# GNU make 로 떨어진다 (별도 설치 없이 돌게 하려는 것이다).

[CmdletBinding()]
param(
    [ValidateSet('arm64-v8a', 'armeabi-v7a', 'x86_64')]
    [string]$Abi = 'arm64-v8a',
    [int]$ApiLevel = 24,
    [string]$NdkRoot = '',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

function Find-Ndk {
    param([string]$Explicit)

    if ($Explicit) { return $Explicit }
    foreach ($name in 'ANDROID_NDK_ROOT', 'ANDROID_NDK_HOME') {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value -and (Test-Path $value)) { return $value }
    }
    $roots = @()
    if ($env:ANDROID_HOME) { $roots += (Join-Path $env:ANDROID_HOME 'ndk') }
    $roots += (Join-Path $env:LOCALAPPDATA 'Android\Sdk\ndk')
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        # 여러 버전이 깔려 있으면 가장 높은 것을 쓴다.
        $latest = Get-ChildItem $root -Directory | Sort-Object Name -Descending | Select-Object -First 1
        if ($latest) { return $latest.FullName }
    }
    throw "Android NDK not found. Set ANDROID_NDK_ROOT or pass -NdkRoot."
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$ndk = Find-Ndk -Explicit $NdkRoot
$toolchain = Join-Path $ndk 'build\cmake\android.toolchain.cmake'
if (-not (Test-Path $toolchain)) { throw "NDK toolchain file missing: $toolchain" }

$buildDir = Join-Path $repoRoot "Build\neocore-unity-android-$Abi"
$pluginDir = Join-Path $PSScriptRoot "com.neocore.unity\Runtime\Plugins\Android\$Abi"

if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    $resolvedBuild = (Resolve-Path -LiteralPath $buildDir).Path
    $buildRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'Build'))
    if (-not $resolvedBuild.StartsWith($buildRoot + [System.IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to clean a directory outside the repository build output'
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

# 생성기 고르기: ninja 가 있으면 ninja, 없으면 NDK 의 make.
$generator = 'Ninja'
$makeProgram = $null
$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if ($ninja) {
    $makeProgram = $ninja.Source
} else {
    $ndkMake = Join-Path $ndk 'prebuilt\windows-x86_64\bin\make.exe'
    if (-not (Test-Path $ndkMake)) { throw "neither ninja nor the NDK's make was found" }
    $generator = 'Unix Makefiles'
    $makeProgram = $ndkMake
}

Write-Host "==> NDK       $ndk" -ForegroundColor Cyan
Write-Host "==> generator $generator ($makeProgram)" -ForegroundColor Cyan

& cmake -S $repoRoot -B $buildDir -G $generator `
    "-DCMAKE_MAKE_PROGRAM=$makeProgram" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DANDROID_ABI=$Abi" `
    "-DANDROID_PLATFORM=android-$ApiLevel" `
    '-DCMAKE_BUILD_TYPE=Release' `
    '-DNEOCORE_BUILD_UNITY_BRIDGE=ON' `
    '-DNEOCORE_BUILD_EXAMPLES=OFF' `
    '-DNEOCORE_BUILD_TESTS=OFF'
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }

& cmake --build $buildDir --target NeoCoreUnity --parallel
if ($LASTEXITCODE -ne 0) { throw "cmake build failed ($LASTEXITCODE)" }

$so = Join-Path $buildDir 'Unity/NeoCore/libNeoCoreUnity.so'
if (-not (Test-Path $so)) { throw "libNeoCoreUnity.so not found under $buildDir" }

# 배포 전에 C ABI 외부의 C++ 심볼이 노출되지 않았는지 확인한다.
$nm = Join-Path $ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-nm.exe'
$symbols = & $nm --dynamic --defined-only $so
if ($LASTEXITCODE -ne 0) { throw "could not inspect plugin exports" }
$unexpected = @($symbols | Where-Object { $_.Trim() -and $_ -notmatch '\sNc[A-Za-z0-9_]+$' })
if ($unexpected.Count -ne 0) {
    throw ("Plugin exports symbols outside the Nc C ABI: " + ($unexpected -join ', '))
}
if (-not ($symbols -match '\sNcCompile$')) { throw "Plugin C ABI exports are missing" }

New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
Copy-Item (Join-Path $repoRoot "THIRD_PARTY_NOTICES.txt") (Join-Path $repoRoot "Unity/com.neocore.unity/THIRD_PARTY_NOTICES.txt") -Force
Copy-Item (Join-Path $repoRoot "LICENSE") (Join-Path $repoRoot "Unity/com.neocore.unity/LICENSE") -Force
Copy-Item $so (Join-Path $pluginDir 'libNeoCoreUnity.so') -Force

$info = Get-Item (Join-Path $pluginDir 'libNeoCoreUnity.so')
Write-Host "==> deployed $($info.FullName) ($([math]::Round($info.Length/1KB)) KB)" -ForegroundColor Green

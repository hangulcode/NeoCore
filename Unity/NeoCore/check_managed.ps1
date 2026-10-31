[CmdletBinding()]
param([string]$UnityManaged = '')

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$testDir = Join-Path $repoRoot 'Build\neocore-managed-smoke'
$packageDir = Join-Path $repoRoot 'Unity\com.neocore.unity'
$nativePlugin = Join-Path $packageDir 'Runtime\Plugins\x86_64\NeoCoreUnity.dll'
if (-not (Test-Path -LiteralPath $nativePlugin)) { throw 'Build the typed plugin first' }
$sdkVersion = & dotnet --version
if ($LASTEXITCODE -ne 0) { throw '.NET SDK is required for the managed smoke' }
$targetFramework = 'net' + $sdkVersion.Split('.')[0] + '.0'
New-Item -ItemType Directory -Force -Path $testDir | Out-Null
$project = @"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><OutputType>Exe</OutputType><TargetFramework>$targetFramework</TargetFramework><EnableDefaultCompileItems>false</EnableDefaultCompileItems><LangVersion>7.3</LangVersion><Nullable>disable</Nullable><ImplicitUsings>disable</ImplicitUsings></PropertyGroup>
  <ItemGroup>
    <Compile Include="$packageDir/Runtime/*.cs" />
    <Compile Include="$PSScriptRoot/ManagedSmoke.cs" />
    <None Include="$nativePlugin" Link="NeoCoreUnity.dll" CopyToOutputDirectory="Always" />
  </ItemGroup>
</Project>
"@
$projectFile = Join-Path $testDir 'ManagedSmoke.csproj'
Set-Content -LiteralPath $projectFile -Value $project -Encoding utf8
& dotnet run --project $projectFile --configuration Release -- (Join-Path $packageDir 'Samples~\Basic\demo.nc')
if ($LASTEXITCODE -ne 0) { throw "Managed smoke failed ($LASTEXITCODE)" }

if ($UnityManaged) {
    if (-not (Test-Path -LiteralPath (Join-Path $UnityManaged 'UnityEngine\UnityEngine.CoreModule.dll'))) { throw 'UnityManaged must point to Editor/Data/Managed' }
    $compileProject = @"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><TargetFramework>$targetFramework</TargetFramework><EnableDefaultCompileItems>false</EnableDefaultCompileItems><LangVersion>7.3</LangVersion><Nullable>disable</Nullable><ImplicitUsings>disable</ImplicitUsings><DefineConstants>UNITY_EDITOR</DefineConstants></PropertyGroup>
  <ItemGroup>
    <Compile Include="$packageDir/Runtime/*.cs" />
    <Compile Include="$packageDir/Editor/*.cs" />
    <Compile Include="$packageDir/Samples~/Basic/*.cs" />
    <Compile Include="$PSScriptRoot/UnitySmoke.cs" />
    <Reference Include="$UnityManaged/UnityEngine/*.dll" />
  </ItemGroup>
</Project>
"@
    $compileFile = Join-Path $testDir 'UnityCompile.csproj'
    Set-Content -LiteralPath $compileFile -Value $compileProject -Encoding utf8
    & dotnet build $compileFile --configuration Release --nologo --verbosity quiet
    if ($LASTEXITCODE -ne 0) { throw "Unity API compile check failed ($LASTEXITCODE)" }
}

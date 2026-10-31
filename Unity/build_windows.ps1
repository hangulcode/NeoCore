# 기존 진입점에서도 NeoCore 정적 타입 플러그인을 빌드한다.
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$Generator = '',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'NeoCore/build_windows.ps1') @PSBoundParameters

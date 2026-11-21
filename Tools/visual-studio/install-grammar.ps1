$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot '../vscode-neocore/syntaxes/nc.tmLanguage.json'
$destination = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.vs/Extensions/NeoCore/Syntaxes'
[void][System.IO.Directory]::CreateDirectory($destination)
Copy-Item -LiteralPath $source -Destination (Join-Path $destination 'nc.tmLanguage.json') -Force
Write-Output 'NeoCore syntax highlighting installed. Close and reopen any open .nc tabs in Visual Studio.'

<#
.SYNOPSIS
    Installs the Thunder extension into VS Code on Windows.
.DESCRIPTION
    The extension is plain JavaScript with no dependencies and no build step, so "installing" it
    means putting it where VS Code looks for extensions ($HOME\.vscode\extensions). The files are
    copied by default; -Link makes a directory junction instead, so editing the sources here
    changes the installed extension (reload the window to see it). A junction needs no
    administrator rights; a symbolic link would.
.EXAMPLE
    .\scripts\install.ps1
    .\scripts\install.ps1 -Link
    .\scripts\install.ps1 -Uninstall
    .\scripts\install.ps1 -Target "$env:USERPROFILE\.vscode-insiders\extensions"
#>
param(
    [switch]$Link,
    [switch]$Uninstall,
    [string]$Target
)
$ErrorActionPreference = 'Stop'

$here = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$name = 'thunder.thunder-lang'

if (-not $Target) {
    $candidates = @(
        (Join-Path $env:USERPROFILE '.vscode\extensions'),
        (Join-Path $env:USERPROFILE '.vscode-insiders\extensions'),
        (Join-Path $env:USERPROFILE '.vscode-oss\extensions')
    )
    $Target = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $Target) { $Target = $candidates[0] }
}
New-Item -ItemType Directory -Force -Path $Target | Out-Null
$destination = Join-Path $Target $name

if (Test-Path $destination) {
    # Remove-Item on a junction removes the junction, not what it points at.
    cmd /c rmdir /s /q "$destination" 2>$null
    if (Test-Path $destination) { Remove-Item -Recurse -Force $destination }
}

if ($Uninstall) {
    Write-Host "Removed $destination"
    Write-Host 'Reload VS Code (Developer: Reload Window) to finish.'
    exit 0
}

if ($Link) {
    New-Item -ItemType Junction -Path $destination -Target $here | Out-Null
    Write-Host "Linked $destination -> $here"
} else {
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $entries = @('package.json', 'language-configuration.json', 'README.md', 'CHANGELOG.md', 'LICENSE',
                 'src', 'syntaxes', 'snippets', 'data', 'schemas', 'images', 'templates')
    foreach ($entry in $entries) {
        $source = Join-Path $here $entry
        if (Test-Path $source) { Copy-Item -Recurse -Force $source $destination }
    }
    Write-Host "Copied the extension to $destination"
}

Write-Host ''
Write-Host 'Next:'
Write-Host '  1. Reload VS Code: Ctrl+Shift+P -> "Developer: Reload Window"'
Write-Host '  2. Open a .th file. If the toolchain is not found, run'
Write-Host '     "Thunder: Select the Toolchain Directory..." or set "thunder.toolchainPath"'
Write-Host '     to the directory holding thunderc.exe and thundervm.exe.'

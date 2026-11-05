<#
.SYNOPSIS
    Builds the Thunder toolchain (if needed) and starts the Thunder IDE.
.EXAMPLE
    .\run.ps1
    .\run.ps1 samples\09_recursion.th
    .\run.ps1 -NoBuild
#>
param(
    [switch]$NoBuild,
    [string]$File
)
$ErrorActionPreference = 'Stop'
Set-Location -Path $PSScriptRoot

function Get-PythonCommand {
    if (Get-Command py -ErrorAction SilentlyContinue) { return @('py', '-3') }
    if (Get-Command python -ErrorAction SilentlyContinue) { return @('python') }
    throw 'Python 3 was not found. Install it from https://www.python.org/downloads/ (include "tcl/tk and IDLE").'
}

$python = @(Get-PythonCommand)
$arguments = @()
if ($python.Count -gt 1) { $arguments += $python[1..($python.Count - 1)] }
$arguments += (Join-Path $PSScriptRoot 'tools\launch.py')
if ($NoBuild) { $arguments += '--no-build' }
if ($File) { $arguments += $File }

& $python[0] @arguments
exit $LASTEXITCODE

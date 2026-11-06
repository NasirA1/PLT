<#
.SYNOPSIS
    Builds Thunder, sets up the Python environment (PySide6 in .venv, first run only), runs all C++ and
    Python tests and prints a coverage and code-quality summary.
.DESCRIPTION
    C++ coverage with MSVC needs OpenCppCoverage (https://github.com/OpenCppCoverage/OpenCppCoverage);
    without it the C++ coverage is reported as "not measured" and only Python coverage is checked.
.EXAMPLE
    .\run_tests.ps1
    .\run_tests.ps1 -Clean
#>
param(
    [switch]$Clean,
    [switch]$Tidy
)
$ErrorActionPreference = 'Stop'
Set-Location -Path $PSScriptRoot

function Get-PythonCommand {
    if (Get-Command py -ErrorAction SilentlyContinue) { return @('py', '-3') }
    if (Get-Command python -ErrorAction SilentlyContinue) { return @('python') }
    throw 'Python 3 was not found. Install it from https://www.python.org/downloads/, then run: py -3 -m pip install PySide6'
}

$python = @(Get-PythonCommand)
$arguments = @()
if ($python.Count -gt 1) { $arguments += $python[1..($python.Count - 1)] }
$arguments += (Join-Path $PSScriptRoot 'tools\run_tests.py')
if ($Clean) { $arguments += '--clean' }
if ($Tidy) { $arguments += '--tidy' }

& $python[0] @arguments
exit $LASTEXITCODE

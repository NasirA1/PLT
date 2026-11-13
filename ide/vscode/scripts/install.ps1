<#
.SYNOPSIS
    Installs the Thunder extension into VS Code on Windows.

.DESCRIPTION
    Three ways in, in the order the script prefers them:

    1. Default: build a .vsix with ide/vscode/tools/make_vsix.py (Python only - no Node.js, no
       npm, no vsce) and hand it to VS Code itself:
           code --install-extension <file>.vsix --force
       This is the supported route: VS Code unpacks the package, records it in its extensions
       index and reports failures properly.

    2. -Link: make a directory junction from VS Code's extensions folder to this one, so the
       files VS Code loads *are* the sources. Use it while working on the extension: after an
       edit, "Developer: Reload Window" is enough. A junction needs no administrator rights.

    3. -Copy: copy the files into the extensions folder (what -Link does, without the link).
       Used automatically when the "code" command cannot be found.

    An installed .vsix is a snapshot: VS Code pins it and never updates it by itself, because
    it did not come from a marketplace. Run this script again after changing the sources (or
    use -Link, or press F5 in this folder to start an Extension Development Host).

    If PowerShell refuses to run the script, start it as:
        powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1

.EXAMPLE
    .\scripts\install.ps1
    .\scripts\install.ps1 -Link
    .\scripts\install.ps1 -Uninstall
    .\scripts\install.ps1 -Target "$env:USERPROFILE\.vscode-insiders\extensions"
#>
[CmdletBinding()]
param(
    [switch]$Link,
    [switch]$Copy,
    [switch]$Uninstall,
    [string]$Target,
    [string]$CodeCommand
)

$ErrorActionPreference = 'Stop'
# A non-zero exit from code.cmd or python must not abort the script: the failures are handled.
$PSNativeCommandUseErrorActionPreference = $false
# VS Code's command line is Node.js, which prints deprecation warnings on stderr (DEP0169 and
# the like). They are not about this script, so they are switched off at the source.
$env:NODE_NO_WARNINGS = '1'

<#
    Runs a command-line tool and returns what it printed, standard error included, as lines.

    Windows PowerShell 5.1 turns every line a native command writes to standard error into an
    error record when the stream is redirected - and with $ErrorActionPreference = 'Stop' the
    first one stops the script, even though the tool succeeded ("code.cmd : (node:5368)
    [DEP0169] DeprecationWarning ... NativeCommandError"). So the tool runs with the preference
    relaxed, its output is turned back into plain text, and the caller decides what a failure is
    by the exit code, which is kept in $LASTEXITCODE as usual.
#>
function Invoke-Tool([string]$executable, [string[]]$arguments) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = & $executable @arguments 2>&1 | ForEach-Object { "$_" }
    } finally {
        $ErrorActionPreference = $previous
    }
    return @($lines | Where-Object { $_ -notmatch 'DeprecationWarning|--trace-deprecation' })
}

$here = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$manifest = Get-Content (Join-Path $here 'package.json') -Raw | ConvertFrom-Json
$identifier = "$($manifest.publisher).$($manifest.name)"
$folderName = $identifier   # what -Link and -Copy call the directory

function Find-Code {
    if ($CodeCommand) { return $CodeCommand }
    foreach ($name in @('code', 'code-insiders', 'codium')) {
        $found = Get-Command $name -ErrorAction SilentlyContinue
        if ($found) { return $found.Source }
    }
    foreach ($base in @($env:LOCALAPPDATA, $env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if (-not $base) { continue }
        $candidate = Join-Path $base 'Programs\Microsoft VS Code\bin\code.cmd'
        if (Test-Path $candidate) { return $candidate }
        $candidate = Join-Path $base 'Microsoft VS Code\bin\code.cmd'
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Find-Python {
    foreach ($name in @('py', 'python', 'python3')) {
        $found = Get-Command $name -ErrorAction SilentlyContinue
        if ($found) { return $(if ($name -eq 'py') { @($found.Source, '-3') } else { @($found.Source) }) }
    }
    return $null
}

function Get-ExtensionsDirectory {
    if ($Target) { return $Target }
    $candidates = @(
        (Join-Path $env:USERPROFILE '.vscode\extensions'),
        (Join-Path $env:USERPROFILE '.vscode-insiders\extensions'),
        (Join-Path $env:USERPROFILE '.vscode-oss\extensions')
    )
    $existing = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($existing) { return $existing }
    return $candidates[0]
}

function Remove-InstalledDirectory([string]$path) {
    if (-not (Test-Path $path)) { return }
    $item = Get-Item $path -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        # A junction: Delete() removes the link itself, never what it points at. (Remove-Item
        # -Recurse on a junction deletes the target's contents in Windows PowerShell 5.1.)
        try {
            $item.Delete()
        } catch {
            cmd /c rmdir "$path" | Out-Null
        }
    } else {
        Remove-Item -Recurse -Force $path
    }
}

$code = Find-Code

# ---------------------------------------------------------------------- uninstall

if ($Uninstall) {
    $removed = $false
    if ($code -and -not $Target) {
        Invoke-Tool $code @('--uninstall-extension', $identifier) | ForEach-Object { Write-Host $_ }
        $removed = $LASTEXITCODE -eq 0
    }
    $destination = Join-Path (Get-ExtensionsDirectory) $folderName
    if (Test-Path $destination) {
        Remove-InstalledDirectory $destination
        Write-Host "Removed $destination"
        $removed = $true
    }
    if (-not $removed) { Write-Host "$identifier was not installed." }
    Write-Host 'Reload VS Code ("Developer: Reload Window") to finish.'
    exit 0
}

# ---------------------------------------------------------------------- development: a junction

if ($Link -or $Copy -or (-not $code)) {
    if (-not $code -and -not ($Link -or $Copy)) {
        Write-Warning 'The "code" command was not found, so the files are copied into the extensions folder instead of being installed by VS Code. Add VS Code to the PATH ("Shell Command: Install ''code'' command in PATH") for the supported route.'
    }
    $directory = Get-ExtensionsDirectory
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $destination = Join-Path $directory $folderName
    Remove-InstalledDirectory $destination

    if ($Link) {
        try {
            New-Item -ItemType Junction -Path $destination -Target $here -ErrorAction Stop | Out-Null
        } catch {
            $reason = $_.Exception.Message
            Write-Error "A junction could not be created: $reason. Run the script without -Link to copy the files in instead."
            exit 1
        }
        Write-Host "Linked $destination -> $here"
        Write-Host 'Editing the sources here now changes the installed extension.'
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
} else {

# ---------------------------------------------------------------------- the supported route

    # @(...) forces an array; @($null) still has one element, so the emptiness test looks
    # at the element itself.
    $python = @(Find-Python)
    if ($python.Count -eq 0 -or -not $python[0]) {
        Write-Error 'Python 3 was not found, and it builds the package. Install it from https://www.python.org/downloads/, or run this script with -Copy.'
        exit 1
    }
    $maker = Join-Path $here 'tools\make_vsix.py'
    $arguments = @()
    if ($python.Count -gt 1) { $arguments += $python[1..($python.Count - 1)] }
    $arguments += @($maker, '--output', $here)
    Invoke-Tool $python[0] $arguments | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        Write-Error 'The package could not be built.'
        exit 1
    }
    $vsix = Get-ChildItem (Join-Path $here '*.vsix') | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $vsix) {
        Write-Error 'No .vsix was produced.'
        exit 1
    }

    $codeArguments = @('--install-extension', $vsix.FullName, '--force')
    if ($Target) { $codeArguments = @('--extensions-dir', $Target) + $codeArguments }
    Invoke-Tool $code $codeArguments | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        Write-Error ("VS Code could not install $($vsix.Name). Try: $code --install-extension `"$($vsix.FullName)`" --force")
        exit 1
    }

    # Say plainly whether it is really there, rather than assuming it is.
    $listArguments = @('--list-extensions', '--show-versions')
    if ($Target) { $listArguments = @('--extensions-dir', $Target) + $listArguments }
    $installed = Invoke-Tool $code $listArguments | Where-Object { $_ -like "$identifier@*" }
    if ($installed) {
        Write-Host "Installed $installed"
    } else {
        Write-Warning "VS Code reported success but does not list $identifier. Run: $code --list-extensions"
    }
}

Write-Host ''
Write-Host 'Next:'
Write-Host '  1. Reload VS Code: Ctrl+Shift+P -> "Developer: Reload Window"'
Write-Host '  2. Open a .th file. If the toolchain is not found, run'
Write-Host '     "Thunder: Select the Toolchain Directory..." or set "thunder.toolchainPath"'
Write-Host '     to the directory holding thunderc.exe and thundervm.exe.'
if (-not $Link) {
    Write-Host ''
    Write-Host 'This is a snapshot: VS Code will not update it by itself. Run this script again'
    Write-Host 'after changing the sources, or use -Link (or F5 in ide/vscode) while developing.'
}

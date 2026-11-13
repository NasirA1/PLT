#!/usr/bin/env bash
# Installs the Thunder extension into VS Code (Linux and macOS).
#
# Three ways in, in the order the script prefers them:
#
#   1. Default: build a .vsix with tools/make_vsix.py (Python only - no Node.js, no npm, no
#      vsce) and hand it to VS Code itself: "code --install-extension <file>.vsix --force".
#      That is the supported route; VS Code records the extension in its own index and reports
#      failures properly.
#   2. --link: symlink VS Code's extensions folder to this one, so the files VS Code loads
#      *are* the sources. Use it while working on the extension: after an edit,
#      "Developer: Reload Window" is enough.
#   3. --copy: copy the files in (used automatically when "code" cannot be found).
#
# An installed .vsix is a snapshot: VS Code pins it and never updates it by itself, because it
# did not come from a marketplace. Run this script again after changing the sources, or use
# --link, or press F5 in ide/vscode to start an Extension Development Host.
#
# Usage:
#   ./scripts/install.sh                 package it and let VS Code install it
#   ./scripts/install.sh --link          link it for development
#   ./scripts/install.sh --copy          copy it in
#   ./scripts/install.sh --uninstall     remove it again
#   ./scripts/install.sh --target DIR    another extensions directory (Insiders, a remote, ...)
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
identifier="$(python3 -c "import json,sys; m=json.load(open(sys.argv[1])); print(m['publisher'] + '.' + m['name'])" \
    "$here/package.json" 2>/dev/null || echo "thunder.thunder-lang")"
mode="package"
target=""

while [ $# -gt 0 ]; do
    case "$1" in
        --copy) mode="copy" ;;
        --link) mode="link" ;;
        --package|--vsix) mode="package" ;;
        --uninstall) mode="uninstall" ;;
        --target) target="${2:-}"; shift ;;
        -h|--help) sed -n '2,28p' "$0"; exit 0 ;;
        *) echo "install.sh: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

find_code() {
    for name in code code-insiders codium; do
        if command -v "$name" >/dev/null 2>&1; then
            command -v "$name"
            return 0
        fi
    done
    return 1
}

extensions_directory() {
    if [ -n "$target" ]; then
        echo "$target"
        return
    fi
    for candidate in "$HOME/.vscode/extensions" "$HOME/.vscode-server/extensions" \
                     "$HOME/.vscode-oss/extensions" "$HOME/.vscode-insiders/extensions"; do
        if [ -d "$candidate" ]; then echo "$candidate"; return; fi
    done
    echo "$HOME/.vscode/extensions"
}

code="$(find_code || true)"
directory="$(extensions_directory)"
destination="$directory/$identifier"

if [ "$mode" = "uninstall" ]; then
    # Both ways in have to be undone: the package VS Code installed, and a linked or copied
    # directory. VS Code says what it did, so its words are passed straight through.
    if [ -n "$code" ] && [ -z "$target" ]; then
        "$code" --uninstall-extension "$identifier" 2>&1 | grep -v "DeprecationWarning\|trace-deprecation" || true
    fi
    if [ -e "$destination" ] || [ -L "$destination" ]; then
        rm -rf "$destination"
        echo "Removed $destination"
    fi
    echo 'Reload VS Code ("Developer: Reload Window") to finish.'
    exit 0
fi

if [ -z "$code" ] && [ "$mode" = "package" ]; then
    echo "install.sh: the \"code\" command was not found; copying the files in instead." >&2
    echo "            (VS Code: \"Shell Command: Install 'code' command in PATH\")" >&2
    mode="copy"
fi

case "$mode" in
    package)
        "${PYTHON:-python3}" "$here/tools/make_vsix.py" --output "$here"
        vsix="$(ls -t "$here"/*.vsix 2>/dev/null | head -1)"
        if [ -z "$vsix" ]; then
            echo "install.sh: no .vsix was produced." >&2
            exit 1
        fi
        arguments=()
        [ -n "$target" ] && arguments+=(--extensions-dir "$target")
        "$code" "${arguments[@]}" --install-extension "$vsix" --force 2>&1 | grep -v DeprecationWarning || true
        listed="$("$code" "${arguments[@]}" --list-extensions --show-versions 2>/dev/null | grep "^$identifier@" || true)"
        if [ -n "$listed" ]; then
            echo "Installed $listed"
        else
            echo "install.sh: VS Code does not list $identifier. Try: $code --install-extension \"$vsix\" --force" >&2
            exit 1
        fi
        ;;
    link)
        mkdir -p "$directory"
        rm -rf "$destination"
        ln -s "$here" "$destination"
        echo "Linked $destination -> $here"
        echo "Editing the sources here now changes the installed extension."
        ;;
    copy)
        mkdir -p "$directory"
        rm -rf "$destination"
        mkdir -p "$destination"
        for entry in package.json language-configuration.json README.md CHANGELOG.md LICENSE \
                     src syntaxes snippets data schemas images templates; do
            if [ -e "$here/$entry" ]; then cp -r "$here/$entry" "$destination/"; fi
        done
        echo "Copied the extension to $destination"
        ;;
esac

echo
echo "Next:"
echo "  1. Reload VS Code: Ctrl+Shift+P -> \"Developer: Reload Window\""
echo "  2. Open a .th file. If the toolchain is not found, run Thunder: Select the Toolchain Directory..."
echo "     or set \"thunder.toolchainPath\" to the directory holding thunderc and thundervm."
if [ "$mode" != "link" ]; then
    echo
    echo "This is a snapshot: VS Code will not update it by itself. Run this script again after"
    echo "changing the sources, or use --link (or F5 in ide/vscode) while developing."
fi

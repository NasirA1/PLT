#!/usr/bin/env bash
# Installs the Thunder extension into VS Code (Linux and macOS).
#
# The extension is plain JavaScript with no dependencies and no build step, so "installing" it
# means putting it where VS Code looks for extensions. By default a symbolic link is made, so
# editing the sources here changes the installed extension (reload the window to see it).
#
# Usage:
#   ./scripts/install.sh              link it into ~/.vscode/extensions
#   ./scripts/install.sh --copy       copy it instead of linking
#   ./scripts/install.sh --uninstall  remove it again
#   ./scripts/install.sh --target DIR install into another extensions directory
#                                     (VS Code Insiders: ~/.vscode-insiders/extensions)
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
name="thunder.thunder-lang"
mode="link"
target=""

while [ $# -gt 0 ]; do
    case "$1" in
        --copy) mode="copy" ;;
        --link) mode="link" ;;
        --uninstall) mode="uninstall" ;;
        --target) target="${2:-}"; shift ;;
        -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
        *) echo "install.sh: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

if [ -z "$target" ]; then
    for candidate in "$HOME/.vscode/extensions" "$HOME/.vscode-server/extensions" \
                     "$HOME/.vscode-oss/extensions" "$HOME/.vscode-insiders/extensions"; do
        if [ -d "$candidate" ]; then target="$candidate"; break; fi
    done
fi
if [ -z "$target" ]; then
    target="$HOME/.vscode/extensions"
fi
mkdir -p "$target"
destination="$target/$name"

if [ "$mode" = "uninstall" ]; then
    rm -rf "$destination"
    echo "Removed $destination"
    echo "Reload VS Code (Developer: Reload Window) to finish."
    exit 0
fi

rm -rf "$destination"
if [ "$mode" = "link" ]; then
    ln -s "$here" "$destination"
    echo "Linked $destination -> $here"
else
    mkdir -p "$destination"
    # Everything but the tests and the generator, which the editor does not need.
    for entry in package.json language-configuration.json README.md CHANGELOG.md LICENSE \
                 src syntaxes snippets data schemas images templates; do
        if [ -e "$here/$entry" ]; then cp -r "$here/$entry" "$destination/"; fi
    done
    echo "Copied the extension to $destination"
fi

echo
echo "Next:"
echo "  1. Reload VS Code: Ctrl+Shift+P -> \"Developer: Reload Window\""
echo "  2. Open a .th file. If the toolchain is not found, run Thunder: Select the Toolchain Directory..."
echo "     or set \"thunder.toolchainPath\" to the directory holding thunderc and thundervm."

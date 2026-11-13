#!/usr/bin/env python3
"""Packages the extension as a .vsix, with nothing but the Python standard library.

A .vsix is an Open Packaging Conventions archive - a ZIP with three things in it:

    extension.vsixmanifest   what VS Code reads to identify and install the extension
    [Content_Types].xml      the media type of every file extension in the archive
    extension/...            the extension itself, package.json at its root

``vsce`` (the official tool) produces the same thing, but needs Node.js and a network
installation; the Thunder toolchain deliberately needs neither, so the package is built here.
What goes in is what package.json promises and what .vscodeignore does not exclude.

    python3 ide/vscode/tools/make_vsix.py                  # writes thunder-lang-<version>.vsix
    python3 ide/vscode/tools/make_vsix.py --output dist    # into another directory
    python3 ide/vscode/tools/make_vsix.py --list           # only say what would go in

Install the result with VS Code itself:

    code --install-extension thunder-lang-0.2.0.vsix --force
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import sys
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape, quoteattr

EXTENSION = Path(__file__).resolve().parent.parent

# Files that never belong in a package, whatever .vscodeignore says.
ALWAYS_EXCLUDED = [
    ".git/**", ".gitignore", "**/__pycache__/**", "**/*.pyc", "*.vsix", "node_modules/**",
    ".vscode-test/**", ".DS_Store",
]

# The media types VS Code's own packages declare. Anything else is a plain octet-stream.
CONTENT_TYPES = {
    ".json": "application/json",
    ".js": "application/javascript",
    ".md": "text/markdown",
    ".txt": "text/plain",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".th": "text/plain",
    ".code-snippets": "application/json",
    ".vsixmanifest": "text/xml",
    ".xml": "text/xml",
    ".ps1": "text/plain",
    ".sh": "text/plain",
    ".py": "text/plain",
}


def read_manifest() -> dict:
    return json.loads((EXTENSION / "package.json").read_text(encoding="utf-8"))


def ignore_patterns() -> list[str]:
    """The patterns in .vscodeignore, in the same shape as git's ignore patterns."""
    patterns = list(ALWAYS_EXCLUDED)
    ignore_file = EXTENSION / ".vscodeignore"
    if ignore_file.is_file():
        for line in ignore_file.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                patterns.append(line)
    return patterns


def is_excluded(relative: str, patterns: list[str]) -> bool:
    """True if a path (with / separators, relative to the extension) is excluded."""
    for pattern in patterns:
        if pattern.endswith("/**"):
            prefix = pattern[:-3]
            if relative == prefix or relative.startswith(prefix + "/"):
                return True
        if fnmatch.fnmatch(relative, pattern) or fnmatch.fnmatch(Path(relative).name, pattern):
            return True
        # "test/**" style patterns also match everything under a matching directory.
        if "/" in pattern and fnmatch.fnmatch(relative, pattern.rstrip("/") + "/*"):
            return True
    return False


def files_to_package() -> list[Path]:
    """Every file that goes into the package, in a stable order."""
    patterns = ignore_patterns()
    found = []
    for path in sorted(EXTENSION.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(EXTENSION).as_posix()
        if is_excluded(relative, patterns):
            continue
        found.append(path)
    return found


def vsix_manifest(manifest: dict, assets: list[tuple[str, str]]) -> str:
    """extension.vsixmanifest: who the extension is, and which VS Code it needs."""
    identity = {
        "Id": manifest["name"],
        "Version": manifest["version"],
        "Publisher": manifest.get("publisher", "local"),
    }
    properties = [
        ("Microsoft.VisualStudio.Code.Engine", manifest.get("engines", {}).get("vscode", "*")),
        ("Microsoft.VisualStudio.Code.ExtensionDependencies", ""),
        ("Microsoft.VisualStudio.Code.ExtensionPack", ""),
        ("Microsoft.VisualStudio.Code.ExtensionKind", "workspace"),
        ("Microsoft.VisualStudio.Code.LocalizedLanguages", ""),
    ]
    repository = manifest.get("repository")
    if isinstance(repository, dict) and repository.get("url"):
        properties.append(("Microsoft.VisualStudio.Services.Links.Source", repository["url"]))
        properties.append(("Microsoft.VisualStudio.Services.Links.Getstarted", repository["url"]))
    elif isinstance(repository, str):
        properties.append(("Microsoft.VisualStudio.Services.Links.Source", repository))

    lines = [
        '<?xml version="1.0" encoding="utf-8"?>',
        '<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011"'
        ' xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">',
        "  <Metadata>",
        "    <Identity Language=\"en-US\" "
        + " ".join(f"{key}={quoteattr(value)}" for key, value in identity.items()) + " />",
        f"    <DisplayName>{escape(manifest.get('displayName', manifest['name']))}</DisplayName>",
        f"    <Description xml:space=\"preserve\">{escape(manifest.get('description', ''))}</Description>",
        f"    <Tags>{escape(','.join(manifest.get('keywords', [])))}</Tags>",
        f"    <Categories>{escape(','.join(manifest.get('categories', [])))}</Categories>",
        "    <GalleryFlags>Public</GalleryFlags>",
        "    <Properties>",
    ]
    for key, value in properties:
        lines.append(f"      <Property Id={quoteattr(key)} Value={quoteattr(value)} />")
    lines += [
        "    </Properties>",
        "  </Metadata>",
        "  <Installation>",
        '    <InstallationTarget Id="Microsoft.VisualStudio.Code" />',
        "  </Installation>",
        "  <Dependencies />",
        "  <Assets>",
    ]
    for asset_type, path in assets:
        lines.append(f"    <Asset Type={quoteattr(asset_type)} Path={quoteattr(path)} Addressable=\"true\" />")
    lines += ["  </Assets>", "</PackageManifest>", ""]
    return "\n".join(lines)


def content_types(paths: list[str]) -> str:
    """[Content_Types].xml: one default per file extension the archive contains."""
    suffixes = sorted({Path(path).suffix.lower() for path in paths if Path(path).suffix})
    suffixes = sorted(set(suffixes) | {".vsixmanifest", ".xml", ".json"})
    lines = [
        '<?xml version="1.0" encoding="utf-8"?>',
        '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">',
    ]
    for suffix in suffixes:
        media = CONTENT_TYPES.get(suffix, "application/octet-stream")
        lines.append(f'  <Default Extension={quoteattr(suffix)} ContentType={quoteattr(media)} />')
    # Files without an extension (LICENSE) need a per-part override.
    lines += ["</Types>", ""]
    return "\n".join(lines)


def build(output_directory: Path) -> Path:
    manifest = read_manifest()
    files = files_to_package()
    relative = [path.relative_to(EXTENSION).as_posix() for path in files]
    if "package.json" not in relative:
        raise SystemExit("package.json would not be packaged: check .vscodeignore")

    assets = [("Microsoft.VisualStudio.Code.Manifest", "extension/package.json")]
    for name, asset_type in (("README.md", "Microsoft.VisualStudio.Services.Content.Details"),
                             ("CHANGELOG.md", "Microsoft.VisualStudio.Services.Content.Changelog"),
                             ("LICENSE", "Microsoft.VisualStudio.Services.Content.License")):
        if name in relative:
            assets.append((asset_type, f"extension/{name}"))

    output_directory.mkdir(parents=True, exist_ok=True)
    target = output_directory / f"{manifest['name']}-{manifest['version']}.vsix"
    if target.exists():
        target.unlink()
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("extension.vsixmanifest", vsix_manifest(manifest, assets))
        archive.writestr("[Content_Types].xml", content_types(relative + ["LICENSE.txt"]))
        for path, name in zip(files, relative):
            archive.write(path, f"extension/{name}")
    return target


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", default=str(EXTENSION), help="where to write the .vsix")
    parser.add_argument("--list", action="store_true", help="print what would be packaged and stop")
    arguments = parser.parse_args(argv)

    if arguments.list:
        for path in files_to_package():
            print(path.relative_to(EXTENSION).as_posix())
        return 0
    target = build(Path(arguments.output).resolve())
    size = target.stat().st_size
    print(f"wrote {target} ({size // 1024} KiB)")
    print("install it with:")
    print(f"  code --install-extension {target} --force")
    return 0


if __name__ == "__main__":
    sys.exit(main())

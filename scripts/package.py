#!/usr/bin/env python3
"""Package the Windows x64 extension using Python 3 standard library only."""

import argparse
import hashlib
from pathlib import Path
import re
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parent.parent
BINARY_NAMES = {"ExplorerExtensions.dll", "ExplorerExtensions.exe"}


def package(destination: Path) -> None:
    binaries = ROOT / "out/explorer-extensions"
    templates = ROOT / "packaging/explorer-extensions"
    checksum_data = (binaries / "SHA256SUMS").read_bytes()
    entries = checksum_data.decode("ascii").splitlines()
    if len(entries) != 2:
        raise ValueError("Expected checksums for two Windows binaries")
    # Archive the very bytes checked here; never reread an unverified binary.
    contents = {"SHA256SUMS": checksum_data}
    seen = set()
    for entry in entries:
        match = re.fullmatch(r"([0-9a-f]{64})  (ExplorerExtensions\.(dll|exe))", entry)
        if not match:
            raise ValueError("Invalid checksum entry")
        expected, name, _ = match.groups()
        if name in seen:
            raise ValueError(f"Duplicate checksum entry: {name}")
        seen.add(name)
        contents[name] = (binaries / name).read_bytes()
        if hashlib.sha256(contents[name]).hexdigest() != expected:
            raise ValueError(f"Checksum mismatch: {name}")
    if seen != BINARY_NAMES:
        raise ValueError("Missing Windows binary")
    for name in ("README.md", "THIRD_PARTY_NOTICES.txt", "MinHook-LICENSE.txt"):
        contents[name] = (templates / name).read_bytes()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="explorer-extensions-package-", dir=destination.parent) as temporary:
        archive = Path(temporary) / "package.zip"
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
            for name, data in sorted(contents.items()):
                output.writestr(name, data)
        archive.replace(destination)
    print(f"Packaged: {destination}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=ROOT / "dist/ExplorerExtensions-x64.zip")
    args = parser.parse_args()
    try:
        package(args.destination.resolve())
    except (OSError, ValueError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")


if __name__ == "__main__":
    main()

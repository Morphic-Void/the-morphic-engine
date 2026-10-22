# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)
"""Acquire the pinned SPIR-V-capable DXC distribution into build/dependencies."""
import hashlib
import pathlib
import sys
import tarfile
import urllib.request
import zipfile

VERSION = "v1.9.2609"
PACKAGES = {
    "win32": ("dxc_2026_09_29.zip", "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1"),
    "linux": ("linux_dxc_2026_09_28.x86_x64.tar.gz", "96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1"),
}


def main():
    name, digest = PACKAGES[sys.platform]
    root = pathlib.Path(__file__).resolve().parents[1] / "build" / "dependencies"
    root.mkdir(parents=True, exist_ok=True)
    archive = root / name
    if not archive.exists():
        urllib.request.urlretrieve(
            f"https://github.com/microsoft/DirectXShaderCompiler/releases/download/{VERSION}/{name}", archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f"DXC checksum mismatch: {archive}")
    destination = root / "dxc"
    if name.endswith(".zip"):
        with zipfile.ZipFile(archive) as package:
            package.extractall(destination)
    else:
        with tarfile.open(archive) as package:
            # Python 3.10 compatibility: reject links and paths outside destination.
            for member in package.getmembers():
                target = (destination / member.name).resolve()
                if not target.is_relative_to(destination.resolve()) or member.issym() or member.islnk():
                    raise RuntimeError(f"Unsafe DXC archive member: {member.name}")
            package.extractall(destination)
    (destination / "morphic-version.txt").write_text(VERSION + "\n", encoding="utf-8")
    print(f"Verified {VERSION}: {destination}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Install the Qt version used by Misign, including Qt PDF.

Qt itself is installed with aqtinstall. Since Qt 6.8, Qt PDF ships in the
"extensions" repository, which aqtinstall 3.3 cannot install for recent
versions, so this script downloads it directly from the same mirror.

Requires aqtinstall (`pip install aqtinstall`), which also provides py7zr.
"""

import argparse
import hashlib
import subprocess
import sys
import tempfile
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

# Single source of truth for the Qt version (see docs/adr/0006-qt-version.md).
QT_VERSION = "6.11.3"

# Extra aqt modules. Qt TaskTree is only needed to silence a CMake warning: Qt's own
# QmlAssetDownloader plugin depends on it.
QT_MODULES = ["qttasktree"]

REPOSITORY = "https://download.qt.io/online/qtsdkrepository"

# aqt architecture -> (aqt host, repository host, extension subdirectory, install subdirectory)
ARCHITECTURES = {
    "linux_gcc_64": ("linux", "linux_x64", "x86_64", "gcc_64"),
    "win64_msvc2022_64": ("windows", "windows_x86", "msvc2022_64", "msvc2022_64"),
}


def download(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read()


def install_qt(version: str, arch: str, output_dir: Path) -> None:
    aqt_host = ARCHITECTURES[arch][0]
    with tempfile.TemporaryDirectory() as tmp:
        # The Qt mirrors only publish SHA-1 checksums; aqtinstall expects SHA-256 by default.
        config = Path(tmp) / "aqt.ini"
        config.write_text("[requests]\nhash_algorithm : sha1\n")
        subprocess.run(
            [sys.executable, "-m", "aqt", "-c", str(config), "install-qt", aqt_host, "desktop",
             version, arch, "-O", str(output_dir), "-m", *QT_MODULES],
            check=True,
        )


def install_extension(name: str, version: str, arch: str, prefix: Path) -> None:
    _, repo_host, ext_subdir, _ = ARCHITECTURES[arch]
    compact_version = version.replace(".", "")
    base = f"{REPOSITORY}/{repo_host}/extensions/{name}/{compact_version}/{ext_subdir}"
    package_name = f"extensions.{name}.{compact_version}.{arch}"

    updates = ET.fromstring(download(f"{base}/Updates.xml"))
    package = next(
        (p for p in updates.iter("PackageUpdate") if p.findtext("Name") == package_name), None
    )
    if package is None:
        sys.exit(f"{package_name} not found in {base}/Updates.xml")

    import py7zr  # provided by aqtinstall

    package_version = package.findtext("Version")
    archives = [a.strip() for a in package.findtext("DownloadableArchives", "").split(",") if a.strip()]
    for archive in archives:
        url = f"{base}/{package_name}/{package_version}{archive}"
        print(f"Downloading {url}")
        data = download(url)
        expected_sha1 = download(f"{url}.sha1").decode().split()[0]
        if hashlib.sha1(data).hexdigest() != expected_sha1:
            sys.exit(f"Checksum mismatch for {archive}")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / archive
            path.write_bytes(data)
            with py7zr.SevenZipFile(path) as seven_zip:
                seven_zip.extractall(prefix)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", default=QT_VERSION, help=f"Qt version (default: {QT_VERSION})")
    parser.add_argument("--arch", default="linux_gcc_64", choices=sorted(ARCHITECTURES))
    parser.add_argument("--output-dir", type=Path, required=True, help="Qt root directory, e.g. ~/Qt")
    parser.add_argument("--print-prefix", action="store_true",
                        help="Only print the CMAKE_PREFIX_PATH for this version and architecture")
    args = parser.parse_args()

    prefix = args.output_dir.expanduser().resolve() / args.version / ARCHITECTURES[args.arch][3]
    if not args.print_prefix:
        install_qt(args.version, args.arch, args.output_dir.expanduser().resolve())
        install_extension("qtpdf", args.version, args.arch, prefix)
    print(prefix)


if __name__ == "__main__":
    main()

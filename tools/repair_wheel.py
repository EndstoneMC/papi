"""Repair the PAPI wheel for Linux manylinux.

Runs auditwheel with ``--exclude`` to avoid bundling a duplicate libc++ (which
would conflict with Endstone's copy and segfault), then patches ``_papi.so``'s
NEEDED entries from the standard SONAMEs (``libc++.so.1``, ``libc++abi.so.1``)
to Endstone's auditwheel-hashed SONAMEs (``libc++-<hash>.so.1.0`` etc.) using
``patchelf --replace-needed``.

This establishes deterministic runtime ownership at **build time**: the NEEDED
entries in the shipped wheel match Endstone's bundled libc++ SONAMEs, and
``DT_RPATH "$ORIGIN/../endstone.libs:$ORIGIN"`` (set via ``--disable-new-dtags``
in CMakeLists.txt so it propagates to transitive dependencies) resolves them
from ``endstone.libs/``.  No import-time mutation of site-packages is needed.

Fail-closed: if Endstone is not installed, no hashed lib is found, multiple
ambiguous matches exist, or patchelf is unavailable, the script exits non-zero
and the wheel build fails.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Standard SONAME -> prefix that auditwheel uses for the hashed rename.
_LIB_MAP = {
    "libc++.so.1": "libc++-",
    "libc++abi.so.1": "libc++abi-",
}


def _find_endstone_hashed_sonames() -> dict[str, str]:
    """Return ``{standard_soname: hashed_soname}`` from Endstone's endstone.libs/."""
    try:
        import endstone
    except ImportError:
        sys.exit("repair_wheel: endstone is not installed; cannot determine libc++ SONAMEs")
    libs_dir = Path(endstone.__file__).parent.parent / "endstone.libs"
    if not libs_dir.is_dir():
        sys.exit(f"repair_wheel: endstone.libs not found at {libs_dir}")

    result: dict[str, str] = {}
    for soname, prefix in _LIB_MAP.items():
        matches = sorted(libs_dir.glob(f"{prefix}*.so.*"))
        if not matches:
            sys.exit(f"repair_wheel: no hashed lib found for {soname} (prefix {prefix!r}) in {libs_dir}")
        if len(matches) > 1:
            sys.exit(f"repair_wheel: multiple hashed libs found for {soname} in {libs_dir}: {matches}")
        result[soname] = matches[0].name
    return result


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} <wheel> <dest_dir>")
    wheel = Path(sys.argv[1])
    dest_dir = Path(sys.argv[2])

    patchelf = shutil.which("patchelf")
    if patchelf is None:
        sys.exit("repair_wheel: patchelf not found on PATH")

    # Step 1: Run auditwheel with --exclude to avoid bundling a duplicate libc++.
    subprocess.check_call(
        [
            "auditwheel",
            "repair",
            "--exclude",
            "libc++.so.1",
            "--exclude",
            "libc++abi.so.1",
            "-w",
            str(dest_dir),
            str(wheel),
        ]
    )

    # Step 2: Find the repaired wheel (auditwheel outputs exactly one).
    repaired_wheels = list(dest_dir.glob("*.whl"))
    if len(repaired_wheels) != 1:
        sys.exit(f"repair_wheel: expected one repaired wheel in {dest_dir}, got {repaired_wheels}")
    repaired = repaired_wheels[0]

    # Step 3: Find Endstone's hashed SONAMEs.
    replacements = _find_endstone_hashed_sonames()

    # Step 4: Unpack, patchelf --replace-needed, repack.
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.check_call([sys.executable, "-m", "wheel", "unpack", str(repaired), "-d", tmp])
        unpacked = list(Path(tmp).iterdir())
        if len(unpacked) != 1:
            sys.exit(f"repair_wheel: expected one unpacked directory, got {unpacked}")
        pkg_root = unpacked[0]

        so_files = list(pkg_root.rglob("_papi*.so"))
        if not so_files:
            sys.exit(f"repair_wheel: _papi*.so not found in unpacked wheel {pkg_root}")
        for so in so_files:
            for old, new in replacements.items():
                subprocess.check_call([patchelf, "--replace-needed", old, new, str(so)])

        # wheel pack regenerates RECORD with the patched .so's new hash.
        repaired.unlink()
        subprocess.check_call([sys.executable, "-m", "wheel", "pack", str(pkg_root), "-d", str(dest_dir)])

    print(f"repair_wheel: patched NEEDED entries -> {replacements}")


if __name__ == "__main__":
    main()

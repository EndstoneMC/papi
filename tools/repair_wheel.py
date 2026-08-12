"""Repair the PAPI wheel for Linux manylinux.

Runs auditwheel with ``--exclude`` to avoid bundling a duplicate libc++ (which
would conflict with Endstone's copy and segfault), then adds tiny standard-SONAME
bridge DSOs beside ``_papi.so``. Each bridge has no runtime implementation of its
own; it depends on the corresponding auditwheel-hashed Endstone DSO.

This establishes deterministic runtime ownership at **build time**: _papi's
standard NEEDED entries resolve to the bridges through ``$ORIGIN``, while each
bridge's old-style RPATH resolves its hashed dependency from ``endstone.libs/``.
No import-time mutation of site-packages is needed.

Fail-closed: if Endstone is not installed, no hashed lib is found, multiple
ambiguous matches exist, or patchelf is unavailable, the script exits non-zero
and the wheel build fails.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

# Standard SONAME -> prefix that auditwheel uses for the hashed rename.
_LIB_MAP = {
    "libc++.so.1": "libc++-",
    "libc++abi.so.1": "libc++abi-",
}
_CPP_RUNTIME_PREFIXES = ("libc++", "libc++abi", "libunwind")
_MANYLINUX_PLATFORM = "manylinux_2_31_x86_64"


def _find_backend_tool(name: str) -> str | None:
    """Find a tool on PATH or beside the active PEP 517 backend interpreter."""
    if found := shutil.which(name):
        return found
    candidate = Path(sys.executable).parent / name
    return str(candidate) if candidate.is_file() else None


def _bundled_cpp_runtimes(names: list[str]) -> list[str]:
    return [
        name
        for name in names
        if name.startswith("endstone_papi.libs/")
        and Path(name).name.startswith(_CPP_RUNTIME_PREFIXES)
        and ".so" in Path(name).name
    ]


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


def repair_wheel(wheel: Path, dest_dir: Path) -> Path:
    """Repair one Linux wheel and return the final wheel path."""
    dest_dir.mkdir(parents=True, exist_ok=True)
    patchelf = _find_backend_tool("patchelf")
    if patchelf is None:
        sys.exit("repair_wheel: patchelf not found on PATH")
    compiler_name = os.environ.get("CC", "clang")
    compiler = shutil.which(compiler_name)
    if compiler is None:
        sys.exit(f"repair_wheel: compiler {compiler_name!r} not found")
    compiler_version = subprocess.check_output([compiler, "--version"], text=True).splitlines()[0]
    if re.search(r"\bclang version 20\.", compiler_version) is None:
        sys.exit(f"repair_wheel: Clang 20 is required, got {compiler_version}")

    # Step 1: Run auditwheel with --exclude to avoid bundling a duplicate libc++.
    with tempfile.TemporaryDirectory(prefix="papi-repair-") as repair_tmp:
        repair_dir = Path(repair_tmp)
        # Use the backend environment even when its bin directory is not
        # inherited in PATH (for example, absolute-path pip invocation).
        subprocess.check_call(
            [
                sys.executable,
                "-m",
                "auditwheel",
                "repair",
                "--plat",
                _MANYLINUX_PLATFORM,
                "--only-plat",
                "--exclude",
                "libc++.so.1",
                "--exclude",
                "libc++abi.so.1",
                "--exclude",
                "libunwind.so.1",
                "-w",
                str(repair_dir),
                str(wheel),
            ]
        )

        # Step 2: Find the repaired wheel (auditwheel outputs exactly one).
        repaired_wheels = list(repair_dir.glob("*.whl"))
        if len(repaired_wheels) != 1:
            sys.exit(f"repair_wheel: expected one repaired wheel in {repair_dir}, got {repaired_wheels}")
        repaired = repaired_wheels[0]

        with zipfile.ZipFile(repaired) as archive:
            bundled_runtimes = _bundled_cpp_runtimes(archive.namelist())
        if bundled_runtimes:
            sys.exit(f"repair_wheel: PAPI-owned C++ runtime libraries are forbidden: {bundled_runtimes}")

        # Step 3: Find Endstone's hashed SONAMEs.
        replacements = _find_endstone_hashed_sonames()

        # Step 4: Unpack, add standard-SONAME bridges, and repack. Rewriting
        # _papi.so's NEEDED entries directly to Endstone's hashed names crashes
        # during module initialization, while the standard SONAME path is proven.
        tmp = repair_dir / "unpacked"
        tmp.mkdir()
        subprocess.check_call([sys.executable, "-m", "wheel", "unpack", str(repaired), "-d", tmp])
        unpacked = list(Path(tmp).iterdir())
        if len(unpacked) != 1:
            sys.exit(f"repair_wheel: expected one unpacked directory, got {unpacked}")
        pkg_root = unpacked[0]

        so_files = list(pkg_root.rglob("_papi*.so"))
        if len(so_files) != 1:
            sys.exit(f"repair_wheel: _papi*.so not found in unpacked wheel {pkg_root}")
        module = so_files[0]
        needed = subprocess.check_output([patchelf, "--print-needed", str(module)], text=True).splitlines()
        for soname in replacements:
            if soname not in needed:
                sys.exit(f"repair_wheel: {module.name} does not require {soname}")

        bridge_source = Path(tmp) / "runtime_bridge.c"
        bridge_source.write_text("void papi_runtime_bridge(void) {}\n", encoding="utf-8")
        for soname, hashed_soname in replacements.items():
            bridge = module.parent / soname
            subprocess.check_call(
                [
                    compiler,
                    "-shared",
                    "-fPIC",
                    "-nostdlib",
                    f"-Wl,-soname,{soname}",
                    "-o",
                    str(bridge),
                    str(bridge_source),
                ]
            )
            subprocess.check_call([patchelf, "--add-needed", hashed_soname, str(bridge)])
            subprocess.check_call([patchelf, "--force-rpath", "--set-rpath", "$ORIGIN/../endstone.libs", str(bridge)])

        # wheel pack records the bridge DSOs and regenerates RECORD.
        final_dir = repair_dir / "final"
        final_dir.mkdir()
        subprocess.check_call([sys.executable, "-m", "wheel", "pack", str(pkg_root), "-d", str(final_dir)])
        final_wheels = list(final_dir.glob("*.whl"))
        if len(final_wheels) != 1:
            sys.exit(f"repair_wheel: expected one final wheel in {final_dir}, got {final_wheels}")
        from tools.verify_linux_wheel import inspect_wheel

        inspect_wheel(final_wheels[0])
        final = dest_dir / final_wheels[0].name
        if final.exists():
            sys.exit(f"repair_wheel: destination already exists: {final}")
        shutil.move(str(final_wheels[0]), final)

    print(f"repair_wheel: added standard-SONAME bridges -> {replacements}")
    return final


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} <wheel> <dest_dir>")
    repair_wheel(Path(sys.argv[1]), Path(sys.argv[2]))


if __name__ == "__main__":
    main()

"""Verify and compare PAPI's release-critical Linux wheel runtime contract."""

from __future__ import annotations

import argparse
import csv
import io
import subprocess
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path

_BRIDGES = ("libc++.so.1", "libc++abi.so.1")
_CPP_RUNTIME_PREFIXES = ("libc++", "libc++abi", "libunwind")


@dataclass(frozen=True)
class RuntimeContract:
    dependencies: tuple[str, ...]
    module_rpath: str
    bridge_dependencies: tuple[tuple[str, tuple[str, ...]], ...]
    bridge_rpaths: tuple[tuple[str, str], ...]
    requires_endstone: bool


def _output(*command: str) -> str:
    return subprocess.check_output(command, text=True).strip()


def _is_papi_owned_cpp_runtime(name: str) -> bool:
    if not name.startswith("endstone_papi.libs/") or not name.endswith((".so", ".so.1", ".so.1.0")):
        return False
    return Path(name).name.startswith(_CPP_RUNTIME_PREFIXES)


def inspect_wheel(wheel: Path) -> RuntimeContract:
    with tempfile.TemporaryDirectory(prefix="papi-wheel-inspect-") as temporary_directory:
        root = Path(temporary_directory)
        with zipfile.ZipFile(wheel) as archive:
            names = archive.namelist()
            archive.extractall(root)
            module_names = [name for name in names if Path(name).name.startswith("_papi") and name.endswith(".so")]
            if len(module_names) != 1:
                raise AssertionError(f"expected one _papi module in {wheel}, got {module_names}")
            module = root / module_names[0]

            metadata_names = [name for name in names if name.endswith(".dist-info/METADATA")]
            if len(metadata_names) != 1:
                raise AssertionError(f"expected one METADATA in {wheel}, got {metadata_names}")
            metadata = archive.read(metadata_names[0]).decode()
            requires_endstone = any(
                line.lower().startswith("requires-dist: endstone") for line in metadata.splitlines()
            )

            record_names = [name for name in names if name.endswith(".dist-info/RECORD")]
            if len(record_names) != 1:
                raise AssertionError(f"expected one RECORD in {wheel}, got {record_names}")
            record = {row[0] for row in csv.reader(io.StringIO(archive.read(record_names[0]).decode()))}

        module_package = module.parent
        for bridge_name in _BRIDGES:
            relative = f"endstone_papi/{bridge_name}"
            if relative not in names or relative not in record:
                raise AssertionError(f"{bridge_name} missing from wheel or RECORD in {wheel}")
        if any(_is_papi_owned_cpp_runtime(name) for name in names):
            raise AssertionError(f"PAPI-owned runtime bundle found in {wheel}")
        if not requires_endstone:
            raise AssertionError(f"Endstone runtime dependency missing from {wheel}")

        dependencies = tuple(sorted(_output("patchelf", "--print-needed", str(module)).splitlines()))
        for soname in _BRIDGES:
            if soname not in dependencies:
                raise AssertionError(f"{module.name} does not require {soname}")
        module_rpath = _output("patchelf", "--print-rpath", str(module))
        if module_rpath != "$ORIGIN/../endstone.libs:$ORIGIN":
            raise AssertionError(f"unexpected module RPATH {module_rpath!r}")

        bridge_dependencies = []
        bridge_rpaths = []
        for bridge_name in _BRIDGES:
            bridge = module_package / bridge_name
            needed = tuple(sorted(_output("patchelf", "--print-needed", str(bridge)).splitlines()))
            prefix = bridge_name.removesuffix(".so.1") + "-"
            if len(needed) != 1 or not needed[0].startswith(prefix) or not needed[0].endswith(".so.1.0"):
                raise AssertionError(f"unexpected {bridge_name} dependency: {needed}")
            rpath = _output("patchelf", "--print-rpath", str(bridge))
            if rpath != "$ORIGIN/../endstone.libs":
                raise AssertionError(f"unexpected {bridge_name} RPATH {rpath!r}")
            bridge_dependencies.append((bridge_name, needed))
            bridge_rpaths.append((bridge_name, rpath))

        return RuntimeContract(
            dependencies=dependencies,
            module_rpath=module_rpath,
            bridge_dependencies=tuple(bridge_dependencies),
            bridge_rpaths=tuple(bridge_rpaths),
            requires_endstone=requires_endstone,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("wheel", type=Path)
    parser.add_argument("--compare", type=Path)
    args = parser.parse_args()

    contract = inspect_wheel(args.wheel)
    print(f"verified Linux runtime contract: {args.wheel}")
    if args.compare is not None:
        comparison = inspect_wheel(args.compare)
        if contract != comparison:
            raise AssertionError(f"runtime contracts differ:\n{contract!r}\n{comparison!r}")
        print(f"equivalent Linux runtime contract: {args.compare}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

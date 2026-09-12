"""Verify and compare PAPI's release-critical Linux wheel runtime contract."""

from __future__ import annotations

import argparse
import csv
import io
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path

_BRIDGES = ("libc++.so.1", "libc++abi.so.1", "libunwind.so.1")
_CPP_RUNTIME_PREFIXES = ("libc++", "libc++abi", "libunwind")
_EXPECTED_PLATFORM = "manylinux_2_31_x86_64"
_PLATFORM_PATTERN = re.compile(r"manylinux_(\d+)_(\d+)_x86_64")
_GLIBC_PATTERN = re.compile(r"GLIBC_(\d+)\.(\d+)")


@dataclass(frozen=True)
class RuntimeContract:
    platform: str
    elf_glibc_requirements: tuple[tuple[str, tuple[int, int]], ...]
    dependencies: tuple[str, ...]
    module_rpath: str
    bridge_dependencies: tuple[tuple[str, tuple[str, ...]], ...]
    bridge_rpaths: tuple[tuple[str, str], ...]
    requires_endstone: bool
    gcc_imports: tuple[tuple[str, str, str], ...]


def _output(*command: str) -> str:
    return subprocess.check_output(command, text=True).strip()


def _gcc_imports(path: Path) -> tuple[tuple[str, str, str], ...]:
    from elftools.elf.elffile import ELFFile

    with path.open("rb") as stream:
        elf = ELFFile(stream)
        needed = elf.get_section_by_name(".gnu.version_r")
        versions = {}
        if needed is not None:
            for provider, auxiliaries in needed.iter_versions():
                for auxiliary in auxiliaries:
                    versions[auxiliary["vna_other"] & 0x7FFF] = (provider.name, auxiliary.name)
        symbols = elf.get_section_by_name(".dynsym")
        indices = elf.get_section_by_name(".gnu.version")
        result = []
        if symbols is not None and indices is not None:
            for index, symbol in enumerate(symbols.iter_symbols()):
                if symbol["st_shndx"] != "SHN_UNDEF":
                    continue
                version_index = indices.get_symbol(index)["ndx"]
                if not isinstance(version_index, int):
                    continue
                provider, version = versions.get(version_index & 0x7FFF, ("", ""))
                if version.startswith("GCC_"):
                    result.append((provider, symbol.name, version))
        return tuple(sorted(result))


def _validate_gcc_imports(dependencies: tuple[str, ...], imports: tuple[tuple[str, str, str], ...]) -> None:
    allowed = {
        ("libgcc_s.so.1", "__udivti3", "GCC_3.0"),
        ("libgcc_s.so.1", "__umodti3", "GCC_3.0"),
    }
    if not set(imports) <= allowed:
        raise AssertionError(f"unexpected GCC-versioned imports: {imports}")
    if ("libgcc_s.so.1" in dependencies) != bool(imports):
        raise AssertionError(f"libgcc_s dependency does not match arithmetic imports: {imports}")


def _is_papi_owned_cpp_runtime(name: str) -> bool:
    if not name.startswith("endstone_papi.libs/") or not name.endswith((".so", ".so.1", ".so.1.0")):
        return False
    return Path(name).name.startswith(_CPP_RUNTIME_PREFIXES)


def _platform_version(platform: str) -> tuple[int, int]:
    match = _PLATFORM_PATTERN.fullmatch(platform)
    if match is None:
        raise AssertionError(f"unsupported Linux wheel platform tag: {platform!r}")
    return int(match.group(1)), int(match.group(2))


def _filename_platforms(wheel: Path) -> set[str]:
    if wheel.suffix != ".whl":
        raise AssertionError(f"not a wheel: {wheel}")
    platform_field = wheel.stem.rsplit("-", 1)[-1]
    return set(platform_field.split("."))


def _wheel_metadata_platforms(content: str) -> set[str]:
    platforms = set()
    for line in content.splitlines():
        if not line.startswith("Tag: "):
            continue
        platforms.add(line.rsplit("-", 1)[-1])
    return platforms


def _glibc_requirement(path: Path) -> tuple[int, int] | None:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.gnuversions import GNUVerNeedSection

    maximum = (0, 0)
    with path.open("rb") as stream:
        if stream.read(4) != b"\x7fELF":
            return None
        stream.seek(0)
        elf = ELFFile(stream)
        for section in elf.iter_sections():
            if not isinstance(section, GNUVerNeedSection):
                continue
            for _version, auxiliary_versions in section.iter_versions():
                for auxiliary in auxiliary_versions:
                    match = _GLIBC_PATTERN.fullmatch(auxiliary.name)
                    if match is not None:
                        maximum = max(maximum, (int(match.group(1)), int(match.group(2))))
    return maximum


def _assert_platform_compatible(platform: str, required: tuple[int, int]) -> None:
    advertised = _platform_version(platform)
    if advertised < required:
        raise AssertionError(
            f"wheel advertises {platform}, but packaged ELF objects require GLIBC_{required[0]}.{required[1]}"
        )


def _auditwheel_platform(wheel: Path) -> str:
    import endstone

    runtime = Path(endstone.__file__).resolve().parent.parent / "endstone.libs"
    if not runtime.is_dir():
        raise AssertionError(f"Endstone runtime directory missing: {runtime}")
    environment = dict(os.environ, LD_LIBRARY_PATH=str(runtime))
    environment.pop("LD_PRELOAD", None)
    with tempfile.TemporaryDirectory(prefix="papi-runtime-closure-") as temporary_directory:
        root = Path(temporary_directory)
        with zipfile.ZipFile(wheel) as archive:
            archive.extractall(root)
        module = next((root / "endstone_papi").glob("_papi*.so"))
        closure = subprocess.check_output(["ldd", str(module)], text=True, env=environment)
        if "not found" in closure:
            raise AssertionError(f"unresolved runtime dependency:\n{closure}")
        for line in closure.splitlines():
            fields = line.split()
            if not fields or not fields[0].startswith(_CPP_RUNTIME_PREFIXES):
                continue
            if len(fields) < 3 or fields[1] != "=>":
                raise AssertionError(f"unexpected runtime resolution: {line}")
            resolved = Path(fields[2]).resolve()
            owner = module.parent if fields[0] in _BRIDGES else runtime
            if resolved.parent != owner:
                raise AssertionError(f"runtime resolved outside its owner: {line}")
    output = subprocess.check_output(
        [sys.executable, "-m", "auditwheel", "show", str(wheel)], text=True, env=environment
    )
    constrained = re.search(r'constrains the platform tag to\s+"(manylinux_\d+_\d+_x86_64)"', output)
    if constrained is not None:
        return constrained.group(1)
    consistent = re.search(r'consistent with the following platform tag:\s+"(manylinux_\d+_\d+_x86_64)"', output)
    if consistent is None:
        raise AssertionError(f"could not determine auditwheel platform for {wheel}:\n{output}")
    return consistent.group(1)


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

            wheel_metadata_names = [name for name in names if name.endswith(".dist-info/WHEEL")]
            if len(wheel_metadata_names) != 1:
                raise AssertionError(f"expected one WHEEL metadata file in {wheel}, got {wheel_metadata_names}")
            wheel_metadata = archive.read(wheel_metadata_names[0]).decode()

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

        filename_platforms = _filename_platforms(wheel)
        metadata_platforms = _wheel_metadata_platforms(wheel_metadata)
        if filename_platforms != {_EXPECTED_PLATFORM}:
            raise AssertionError(f"unexpected filename platform tags in {wheel}: {sorted(filename_platforms)}")
        if metadata_platforms != filename_platforms:
            raise AssertionError(
                f"filename/WHEEL platform tags differ in {wheel}: {sorted(filename_platforms)} != "
                f"{sorted(metadata_platforms)}"
            )

        elf_glibc_requirements = []
        for name in names:
            candidate = root / name
            if not candidate.is_file():
                continue
            required = _glibc_requirement(candidate)
            if required is not None:
                elf_glibc_requirements.append((name, required))
        maximum_glibc = max((required for _name, required in elf_glibc_requirements), default=(0, 0))
        _assert_platform_compatible(_EXPECTED_PLATFORM, maximum_glibc)

        dependencies = tuple(sorted(_output("patchelf", "--print-needed", str(module)).splitlines()))
        gcc_imports = _gcc_imports(module)
        _validate_gcc_imports(dependencies, gcc_imports)
        symbols = _output("readelf", "--dyn-syms", "--wide", str(module))
        if any("_Unwind_" in line and " UND " not in line for line in symbols.splitlines()):
            raise AssertionError("_papi contains an unwinder implementation")
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
            if _output("patchelf", "--print-soname", str(bridge)) != bridge_name:
                raise AssertionError(f"unexpected {bridge_name} SONAME")
            dynamic = _output("readelf", "--dynamic", str(bridge))
            if "(RUNPATH)" in dynamic or "(RPATH)" not in dynamic:
                raise AssertionError(f"{bridge_name} must use DT_RPATH")
            exports = _output("nm", "-D", "--defined-only", str(bridge)).splitlines()
            if any(not line.endswith(" papi_runtime_bridge") for line in exports):
                raise AssertionError(f"{bridge_name} contains a runtime implementation")
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
            platform=_EXPECTED_PLATFORM,
            elf_glibc_requirements=tuple(sorted(elf_glibc_requirements)),
            dependencies=dependencies,
            module_rpath=module_rpath,
            bridge_dependencies=tuple(bridge_dependencies),
            bridge_rpaths=tuple(bridge_rpaths),
            requires_endstone=requires_endstone,
            gcc_imports=gcc_imports,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("wheel", type=Path)
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--auditwheel", action="store_true")
    args = parser.parse_args()

    contract = inspect_wheel(args.wheel)
    print(f"verified Linux runtime contract: {args.wheel}")
    if args.auditwheel:
        auditwheel_platform = _auditwheel_platform(args.wheel)
        _assert_platform_compatible(contract.platform, _platform_version(auditwheel_platform))
        print(f"verified auditwheel requirement {auditwheel_platform} fits {contract.platform}")
    if args.compare is not None:
        comparison = inspect_wheel(args.compare)
        if contract != comparison:
            raise AssertionError(f"runtime contracts differ:\n{contract!r}\n{comparison!r}")
        if args.auditwheel:
            comparison_platform = _auditwheel_platform(args.compare)
            _assert_platform_compatible(comparison.platform, _platform_version(comparison_platform))
        print(f"equivalent Linux runtime contract: {args.compare}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

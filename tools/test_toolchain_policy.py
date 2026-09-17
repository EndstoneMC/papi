"""Regressions for ABI-sensitive compiler and backend provenance."""

import importlib.util
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import mock

import tomllib

ROOT = Path(__file__).resolve().parent.parent
REPAIR_SPEC = importlib.util.spec_from_file_location("papi_repair_wheel", ROOT / "tools" / "repair_wheel.py")
assert REPAIR_SPEC is not None and REPAIR_SPEC.loader is not None
repair_wheel = importlib.util.module_from_spec(REPAIR_SPEC)
sys.modules[REPAIR_SPEC.name] = repair_wheel
REPAIR_SPEC.loader.exec_module(repair_wheel)

SMOKE_SPEC = importlib.util.spec_from_file_location("papi_wheel_smoke", ROOT / "tools" / "wheel_smoke_test.py")
assert SMOKE_SPEC is not None and SMOKE_SPEC.loader is not None
wheel_smoke = importlib.util.module_from_spec(SMOKE_SPEC)
sys.modules[SMOKE_SPEC.name] = wheel_smoke
SMOKE_SPEC.loader.exec_module(wheel_smoke)


def test_cmake_requires_endstone_compiler_family_and_records_provenance() -> None:
    source = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    assert 'CMAKE_CXX_COMPILER_ID MATCHES "Clang"' in source
    assert 'CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"' in source
    assert "CMAKE_LINKER_TYPE LLD" in source
    assert "CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18" in source
    assert "toolchain_provenance.txt" in source
    assert "_toolchain_provenance.txt" in source


def test_cmake_compiler_floor_accepts_supported_majors() -> None:
    source = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"CMAKE_CXX_COMPILER_VERSION VERSION_LESS (\d+)", source)
    assert match is not None
    floor = int(match.group(1))
    for major, accepted in ((17, False), (18, True), (20, True), (22, True)):
        assert (major >= floor) is accepted, (major, floor)


def test_manylinux_uses_exact_clang_major_and_verified_runtime_sources() -> None:
    source = (ROOT / "pyproject.toml").read_text(encoding="utf-8")
    runtime_builder = (ROOT / "tools" / "build_linux_runtime.py").read_text(encoding="utf-8")
    config = tomllib.loads(source)["tool"]["cibuildwheel"]["linux"]
    assert config["manylinux-x86_64-image"] == "ghcr.io/endstonemc/manylinux_2_31_x86_64"
    assert 'CC = "/usr/bin/clang-20"' in source
    assert 'CXX = "/usr/bin/clang++-20"' in source
    assert "apt-get update -y -q" in source
    assert "https://apt.llvm.org/llvm.sh" in source
    assert any("/tmp/llvm.sh 20" in command for command in config["before-all"])
    assert any("dpkg-query" in command and "clang-20" in command for command in config["before-all"])
    assert any("clang version 20\\." in command for command in config["before-all"])
    assert "python /project/tools/build_linux_runtime.py" in source
    assert '"20.1.8"' in runtime_builder
    assert "6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d" in runtime_builder
    assert "-DLLVM_ENABLE_RUNTIMES=libunwind;libcxx;libcxxabi" in runtime_builder
    assert "-DLIBCXXABI_USE_LLVM_UNWINDER=ON" in runtime_builder
    assert "-DLIBCXXABI_STATICALLY_LINK_UNWINDER_IN_SHARED_LIBRARY=OFF" in runtime_builder
    assert '"install-unwind"' in runtime_builder
    assert "-DLIBCXX_INCLUDE_TESTS=OFF" in runtime_builder
    assert "-DLIBCXXABI_INCLUDE_TESTS=OFF" in runtime_builder
    assert "clang version (1[89]|[2-9][0-9])" not in source


def test_linux_runtime_bootstrap_retries_transient_download_failures() -> None:
    if sys.platform == "win32" or shutil.which("sh") is None:
        return
    config = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))
    retry = next(
        command for command in config["tool"]["cibuildwheel"]["linux"]["before-all"] if "for attempt in" in command
    )
    with TemporaryDirectory() as temporary_directory:
        root = Path(temporary_directory)
        state = root / "attempts"
        installer = root / "installer.sh"
        wget = root / "wget"
        llvm_script = root / "llvm.sh"
        installer.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        wget.write_text(
            f'#!/bin/sh\nattempt=$(cat "{state}" 2>/dev/null || printf 0)\nattempt=$((attempt + 1))\n'
            f'printf "%s" "$attempt" > "{state}"\n'
            'if [ "$attempt" -le "${FAILURES:-0}" ]; then exit 9; fi\n'
            f'cp "{installer}" "$2"\n',
            encoding="utf-8",
        )
        wget.chmod(0o755)
        command = retry.replace("wget", str(wget), 1).replace("/tmp/llvm.sh", str(llvm_script))
        command = command.replace("sleep 5", ":").replace("sleep 15", ":")

        transient = subprocess.run(
            ["sh", "-c", command],
            env=dict(os.environ, FAILURES="1"),
            capture_output=True,
            text=True,
            check=False,
        )
        assert transient.returncode == 0, transient.stderr
        assert state.read_text(encoding="utf-8") == "2"

        state.unlink()
        permanent = subprocess.run(
            ["sh", "-c", command],
            env=dict(os.environ, FAILURES="3"),
            capture_output=True,
            text=True,
            check=False,
        )
        assert permanent.returncode == 9, permanent.stderr
        assert state.read_text(encoding="utf-8") == "3"


def test_conan_and_pep517_backend_are_exactly_constrained() -> None:
    pyproject = (ROOT / "pyproject.toml").read_text(encoding="utf-8")
    workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
    recipe = (ROOT / "conanfile.py").read_text(encoding="utf-8")
    profile = (ROOT / ".conan2" / "profiles" / "default").read_text(encoding="utf-8")

    assert '"scikit-build-core-conan==0.9.2"' in pyproject
    assert '"conan==2.30.0"' in pyproject
    assert '"pybind11==3.0.1"' in pyproject
    assert workflow.count("conan==2.30.0") == 2
    assert 'Version(str(self.settings.compiler.version)) < "18"' in recipe
    assert "detect_clang_compiler" in profile
    assert 'os.getenv("CC", "clang")' in profile
    assert 'os.getenv("CXX", "clang++")' in profile
    assert "detect_clang_compiler(cpp_compiler)" in profile
    assert '{"c": c_compiler, "cpp": cpp_compiler} | tojson' in profile
    assert "compiler.version=20" not in profile
    assert "/usr/bin/clang-20" not in profile


def test_official_windows_build_still_pins_clang_cl_20() -> None:
    workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
    assert "Select clang-cl 20" in workflow
    assert 'Select-String "clang version 20\\."' in workflow


def test_wheel_validation_requires_compiler_provenance() -> None:
    workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
    template = (ROOT / "cmake" / "toolchain_provenance.txt.in").read_text(encoding="utf-8")

    assert "compiler_version=20." in workflow
    assert "validate_provenance" in (ROOT / "tools" / "wheel_smoke_test.py").read_text(encoding="utf-8")
    assert "wheel_smoke_test.py --official" in workflow
    assert "@CMAKE_CXX_COMPILER_VERSION@" in template

    repair = (ROOT / "tools" / "repair_wheel.py").read_text(encoding="utf-8")
    assert 'os.environ.get("CC", "clang")' in repair
    assert 'r"\\bclang version (\\d+)(?:\\.|\\s|$)"' in repair
    assert "int(clang_match.group(1)) < 18" in repair


def test_wheel_smoke_accepts_developer_clang_and_requires_official_clang20() -> None:
    for major in (18, 19, 20, 22, 200):
        provenance = f"compiler_id=Clang\ncompiler_version={major}.1.8\n"
        wheel_smoke.validate_provenance(provenance)
        try:
            wheel_smoke.validate_provenance(provenance, official=True)
        except AssertionError:
            assert major != 20, major
        else:
            assert major == 20, major
    for provenance in ("", "compiler_id=GNU\ncompiler_version=20.1.8\n", "compiler_id=Clang\n"):
        try:
            wheel_smoke.validate_provenance(provenance)
        except AssertionError:
            pass
        else:
            raise AssertionError(f"invalid provenance accepted: {provenance!r}")


def test_repair_accepts_clang_18_or_newer_and_reaches_auditwheel() -> None:
    versions = [
        "clang version 18.1.8",
        "Ubuntu clang version 19.1.7 (++vendor)",
        "clang version 20.1.8",
        "Ubuntu clang version 21.0.0 (++vendor)",
        "clang version 200.0.0",
    ]

    for version in versions:
        with (
            TemporaryDirectory() as temporary_directory,
            mock.patch.object(repair_wheel, "_find_backend_tool", return_value="/usr/bin/patchelf"),
            mock.patch.object(repair_wheel.shutil, "which", return_value="/usr/bin/clang"),
            mock.patch.object(repair_wheel.subprocess, "check_output", return_value=f"{version}\n"),
            mock.patch.object(
                repair_wheel.subprocess, "check_call", side_effect=RuntimeError("accepted")
            ) as check_call,
        ):
            try:
                repair_wheel.repair_wheel(Path("input.whl"), Path(temporary_directory) / "output")
            except RuntimeError as error:
                assert str(error) == "accepted"
            else:
                raise AssertionError(f"repair did not reach auditwheel for {version!r}")
        assert check_call.call_args.args[0][:4] == [sys.executable, "-m", "auditwheel", "repair"]


def test_repair_rejects_invalid_compilers_before_auditwheel() -> None:
    versions = [
        "clang version 17.0.6",
        "gcc (Ubuntu 13.2.0) 13.2.0",
        "clang version",
        "clang 18.1.8",
        "",
    ]

    for version in versions:
        with (
            TemporaryDirectory() as temporary_directory,
            mock.patch.object(repair_wheel, "_find_backend_tool", return_value="/usr/bin/patchelf"),
            mock.patch.object(repair_wheel.shutil, "which", return_value="/usr/bin/clang"),
            mock.patch.object(repair_wheel.subprocess, "check_output", return_value=f"{version}\n"),
            mock.patch.object(repair_wheel.subprocess, "check_call") as check_call,
        ):
            try:
                repair_wheel.repair_wheel(Path("input.whl"), Path(temporary_directory) / "output")
            except SystemExit as error:
                assert "Clang 18 or newer is required" in str(error)
            else:
                raise AssertionError(f"invalid compiler accepted: {version!r}")
            check_call.assert_not_called()


def test_repair_rejects_missing_compiler_before_version_check() -> None:
    with (
        TemporaryDirectory() as temporary_directory,
        mock.patch.dict(repair_wheel.os.environ, {"CC": "clang"}),
        mock.patch.object(repair_wheel, "_find_backend_tool", return_value="/usr/bin/patchelf"),
        mock.patch.object(repair_wheel.shutil, "which", return_value=None),
        mock.patch.object(repair_wheel.subprocess, "check_output") as check_output,
        mock.patch.object(repair_wheel.subprocess, "check_call") as check_call,
    ):
        try:
            repair_wheel.repair_wheel(Path("input.whl"), Path(temporary_directory) / "output")
        except SystemExit as error:
            assert "compiler 'clang' not found" in str(error)
        else:
            raise AssertionError("missing compiler was accepted")
        check_output.assert_not_called()
        check_call.assert_not_called()


def test_repair_uses_backend_interpreter_and_finds_adjacent_tools() -> None:
    with TemporaryDirectory() as temporary_directory:
        scripts = Path(temporary_directory)
        interpreter = scripts / "python"
        patchelf = scripts / "patchelf"
        interpreter.touch()
        patchelf.touch()

        with (
            mock.patch.object(repair_wheel.sys, "executable", str(interpreter)),
            mock.patch.object(repair_wheel.shutil, "which", return_value=None),
        ):
            assert repair_wheel._find_backend_tool("patchelf") == str(patchelf)

        calls: list[list[str]] = []

        def record_call(command: list[str]) -> None:
            calls.append(command)
            raise RuntimeError("captured")

        with (
            mock.patch.object(repair_wheel.sys, "executable", str(interpreter)),
            mock.patch.object(repair_wheel, "_find_backend_tool", return_value=str(patchelf)),
            mock.patch.object(repair_wheel.shutil, "which", return_value="/usr/bin/clang-20"),
            mock.patch.object(repair_wheel.subprocess, "check_output", return_value="clang version 20.1.8\n"),
            mock.patch.object(repair_wheel.subprocess, "check_call", side_effect=record_call),
        ):
            try:
                repair_wheel.repair_wheel(Path("input.whl"), scripts / "output")
            except RuntimeError as error:
                assert str(error) == "captured"
            else:
                raise AssertionError("repair command was not invoked")

        assert calls[0][:4] == [str(interpreter), "-m", "auditwheel", "repair"]


def test_repair_rejects_only_papi_owned_cpp_runtime_libraries() -> None:
    names = [
        "endstone_papi.libs/",
        "endstone_papi.libs/libother-123.so.2",
        "endstone_papi.libs/libc++-123.so.1.0",
        "endstone_papi.libs/libc++abi-123.so.1.0",
        "endstone_papi.libs/libunwind-123.so.1",
    ]
    assert repair_wheel._bundled_cpp_runtimes(names) == names[2:]


def test_repair_excludes_the_complete_endstone_owned_runtime_family() -> None:
    source = (ROOT / "tools" / "repair_wheel.py").read_text(encoding="utf-8")
    for soname in ("libc++.so.1", "libc++abi.so.1", "libunwind.so.1"):
        assert f'"--exclude",\n                "{soname}"' in source


def test_repair_targets_endstone_manylinux_baseline_and_verifies_final_wheel() -> None:
    source = (ROOT / "tools" / "repair_wheel.py").read_text(encoding="utf-8")
    assert '_MANYLINUX_PLATFORM = "manylinux_2_31_x86_64"' in source
    assert '"--plat",\n                _MANYLINUX_PLATFORM' in source
    assert '"--only-plat"' in source
    assert "inspect_wheel(final_wheels[0])" in source


def main() -> int:
    tests = [value for name, value in globals().items() if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
        print(f"  PASS: {test.__name__}")
    print(f"\n{len(tests)}/{len(tests)} tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Regressions for ABI-sensitive compiler and backend provenance."""

import importlib.util
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


def test_cmake_requires_and_records_clang_20() -> None:
    source = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20" in source
    assert "CMAKE_CXX_COMPILER_VERSION VERSION_LESS 21" in source
    assert "toolchain_provenance.txt" in source
    assert "_toolchain_provenance.txt" in source


def test_manylinux_uses_exact_clang_major_and_verified_runtime_sources() -> None:
    source = (ROOT / "pyproject.toml").read_text(encoding="utf-8")
    runtime_builder = (ROOT / "tools" / "build_linux_runtime.py").read_text(encoding="utf-8")
    config = tomllib.loads(source)["tool"]["cibuildwheel"]["linux"]
    assert 'CC = "/usr/bin/clang-20"' in source
    assert 'CXX = "/usr/bin/clang++-20"' in source
    assert "dnf install -y clang20 " in source
    assert "clang_nvr=%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}" in source
    assert any("rpm -q --qf '%{VERSION}' clang20 | grep -E '^20\\.'" in command for command in config["before-all"])
    assert any("clang version 20\\." in command for command in config["before-all"])
    assert "python /project/tools/build_linux_runtime.py" in source
    assert '"20.1.8"' in runtime_builder
    assert "6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d" in runtime_builder
    assert "-DLIBCXXABI_USE_LLVM_UNWINDER=OFF" in runtime_builder
    assert "-DLIBCXX_INCLUDE_TESTS=OFF" in runtime_builder
    assert "-DLIBCXXABI_INCLUDE_TESTS=OFF" in runtime_builder
    assert "clang version (1[89]|[2-9][0-9])" not in source


def test_conan_and_pep517_backend_are_exactly_constrained() -> None:
    pyproject = (ROOT / "pyproject.toml").read_text(encoding="utf-8")
    workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
    recipe = (ROOT / "conanfile.py").read_text(encoding="utf-8")
    profile = (ROOT / ".conan2" / "profiles" / "default").read_text(encoding="utf-8")

    assert '"scikit-build-core-conan==0.9.2"' in pyproject
    assert '"conan==2.30.0"' in pyproject
    assert '"pybind11==3.0.1"' in pyproject
    assert workflow.count("conan==2.30.0") == 3
    assert 'str(self.settings.compiler.version) != "20"' in recipe
    assert "compiler.version=20" in profile
    assert '"c": "/usr/bin/clang-20"' in profile
    assert '"cpp": "/usr/bin/clang++-20"' in profile
    assert "detect_clang_compiler" not in profile


def test_wheel_validation_requires_compiler_provenance() -> None:
    workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
    smoke = (ROOT / "tools" / "wheel_smoke_test.py").read_text(encoding="utf-8")
    template = (ROOT / "cmake" / "toolchain_provenance.txt.in").read_text(encoding="utf-8")

    assert "compiler_version=20." in workflow
    assert "compiler_version=20." in smoke
    assert "@CMAKE_CXX_COMPILER_VERSION@" in template

    repair = (ROOT / "tools" / "repair_wheel.py").read_text(encoding="utf-8")
    assert 'os.environ.get("CC", "clang")' in repair
    assert 'r"\\bclang version 20\\."' in repair


def test_repair_accepts_vendor_prefixed_clang_20_and_rejects_other_majors() -> None:
    versions = [
        ("clang version 20.1.8", False),
        ("Ubuntu clang version 20.1.8 (++vendor)", False),
        ("Ubuntu clang version 19.1.7", True),
        ("Ubuntu clang version 200.0.0", True),
    ]

    for version, rejected in versions:
        with (
            TemporaryDirectory() as temporary_directory,
            mock.patch.object(repair_wheel.shutil, "which", return_value="/usr/bin/clang-20"),
            mock.patch.object(repair_wheel.subprocess, "check_output", return_value=f"{version}\n"),
            mock.patch.object(repair_wheel.subprocess, "check_call", side_effect=RuntimeError("accepted")),
        ):
            try:
                repair_wheel.repair_wheel(Path("input.whl"), Path(temporary_directory) / "output")
            except SystemExit as error:
                if not rejected:
                    raise AssertionError(f"unexpected rejection for {version!r}: {error}") from error
            except RuntimeError as error:
                if rejected or str(error) != "accepted":
                    raise
            else:
                raise AssertionError("repair test did not reach a terminal result")


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


def main() -> int:
    tests = [value for name, value in globals().items() if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
        print(f"  PASS: {test.__name__}")
    print(f"\n{len(tests)}/{len(tests)} tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Regressions for ABI-sensitive compiler and backend provenance."""

import importlib.util
import subprocess
import sys
import textwrap
import tomllib
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
REPAIR_SPEC = importlib.util.spec_from_file_location("papi_repair_wheel", ROOT / "tools" / "repair_wheel.py")
assert REPAIR_SPEC is not None and REPAIR_SPEC.loader is not None
repair_wheel = importlib.util.module_from_spec(REPAIR_SPEC)
sys.modules[REPAIR_SPEC.name] = repair_wheel
REPAIR_SPEC.loader.exec_module(repair_wheel)


SMOKE_SPEC = importlib.util.spec_from_file_location("papi_wheel_smoke", ROOT / "tools" / "wheel_smoke_test.py")
assert SMOKE_SPEC is not None and SMOKE_SPEC.loader is not None
wheel_smoke = importlib.util.module_from_spec(SMOKE_SPEC)
SMOKE_SPEC.loader.exec_module(wheel_smoke)


def test_wheel_metadata_must_match_the_installed_endstone_snapshot() -> None:
    cases = [
        (["endstone==0.11.11.dev392"], "0.11.11.dev392", True),
        (["endstone==0.11.11.dev392"], "0.11.11.dev391", False),
        (["endstone>=0.12,<0.13"], "0.11.11.dev392", False),
        ([], "0.11.11.dev392", False),
        (["endstone-plugin==0.11.11.dev392"], "0.11.11.dev392", False),
    ]
    for requirements, installed, expected in cases:
        with (
            mock.patch.object(wheel_smoke.metadata, "requires", return_value=requirements),
            mock.patch.object(wheel_smoke.metadata, "version", return_value=installed),
        ):
            try:
                wheel_smoke.validate_runtime_dependency()
            except AssertionError:
                assert not expected, (requirements, installed)
            else:
                assert expected, (requirements, installed)


def test_release_gate_rejects_development_runtime_before_candidate_mutation() -> None:
    workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")
    start = workflow.index('          if [[ "$INPUT_DRY_RUN" != "true" ]]; then')
    end = workflow.index("          PY\n", start)
    gate = textwrap.dedent(workflow[workflow.index("          import tomllib\n", start) : end])
    assert start < workflow.index("git fetch origin main develop --tags")
    assert start < workflow.index("- name: Prepare or reuse immutable candidate")
    with TemporaryDirectory() as temporary_directory:
        config = Path(temporary_directory) / "pyproject.toml"
        for version, accepted in (("0.11.11.dev392", False), ("0.12.0", True)):
            config.write_text(f'[project]\ndependencies = ["endstone=={version}"]\n', encoding="utf-8")
            result = subprocess.run(
                [sys.executable, "-c", gate], cwd=temporary_directory, capture_output=True, text=True, check=False
            )
            assert (result.returncode == 0) == accepted, result.stderr
            if not accepted:
                assert "Production release is blocked" in result.stderr


def test_package_and_workflows_use_verified_binary_snapshot_resolution() -> None:
    config = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))
    assert config["project"]["dependencies"] == ["endstone==0.11.11.dev392"]
    assert any(requirement.startswith("endstone==0.11.11.dev392") for requirement in config["build-system"]["requires"])
    linux = config["tool"]["cibuildwheel"]["linux"]
    assert linux["environment"]["PIP_FIND_LINKS"] == "/project/.endstone-wheelhouse"
    assert linux["environment"]["PIP_ONLY_BINARY"] == "endstone"
    for name in ("build", "release"):
        workflow = (ROOT / ".github" / "workflows" / f"{name}.yml").read_text(encoding="utf-8")
        assert "endstone/archive/" not in workflow
        assert "--no-deps" not in workflow
        assert "tools/prepare_endstone_wheels.py --output-dir .endstone-wheelhouse" in workflow
        assert "PIP_FIND_LINKS=/project/.endstone-wheelhouse PIP_ONLY_BINARY=endstone" in workflow
        assert "python -m pip check" in workflow
        assert "/tmp/papi-sdist-smoke-env/bin/pip check" in workflow


def test_official_wheel_rejects_compiler_drift_but_local_builds_allow_newer_clang() -> None:
    for major in (17, 18, 19, 20, 21, 22, 200):
        for official in (False, True):
            expected = major == 20 if official else major >= 18
            provenance = f"compiler_id=Clang\ncompiler_version={major}.1.8\n"
            try:
                wheel_smoke.validate_provenance(provenance, official=official)
            except AssertionError:
                assert not expected, (major, official)
            else:
                assert expected, (major, official)
    for provenance in ("", "compiler_id=GNU\ncompiler_version=20.1.8\n", "compiler_id=Clang\n"):
        try:
            wheel_smoke.validate_provenance(provenance, official=True)
        except AssertionError:
            pass
        else:
            raise AssertionError(f"invalid provenance accepted: {provenance!r}")


def test_official_wheel_jobs_select_clang_20_and_require_official_provenance() -> None:
    for name, job in (("build", "wheel"), ("release", "build-wheels")):
        workflow = (ROOT / ".github" / "workflows" / f"{name}.yml").read_text(encoding="utf-8")
        wheel_job = workflow[workflow.index(f"  {job}:") :]
        assert 'version: "20.1.8"' in wheel_job
        assert "KyleMayes/install-llvm-action@v2" in wheel_job
        assert wheel_job.index("Select clang-cl 20 (Windows)") < wheel_job.index("- name: Build wheels")
        assert 'Select-String "clang version 20\\."' in wheel_job
        assert "python tools/wheel_smoke_test.py --official" in wheel_job
        assert 'python "$GITHUB_WORKSPACE/tools/wheel_smoke_test.py" --official' in wheel_job


def test_cmake_requires_endstone_compiler_family_and_records_provenance() -> None:
    source = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    assert 'CMAKE_CXX_COMPILER_ID MATCHES "Clang"' in source
    assert 'CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"' in source
    assert "CMAKE_LINKER_TYPE LLD" in source
    assert "CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18" in source
    assert "toolchain_provenance.txt" in source
    assert "_toolchain_provenance.txt" in source


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
    assert "-DLIBCXXABI_USE_LLVM_UNWINDER=ON" in runtime_builder
    assert "-DLLVM_ENABLE_RUNTIMES=libunwind;libcxx;libcxxabi" in runtime_builder
    assert "-DLIBCXXABI_STATICALLY_LINK_UNWINDER_IN_SHARED_LIBRARY=OFF" in runtime_builder
    assert '"install-unwind"' in runtime_builder
    assert "-DLIBCXX_INCLUDE_TESTS=OFF" in runtime_builder
    assert "-DLIBCXXABI_INCLUDE_TESTS=OFF" in runtime_builder
    assert "clang version (1[89]|[2-9][0-9])" not in source


def test_development_conan_profile_detects_clang_18_or_newer() -> None:
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

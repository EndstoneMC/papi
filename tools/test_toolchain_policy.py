"""Static regressions for ABI-sensitive compiler and backend provenance."""

from pathlib import Path

import tomllib

ROOT = Path(__file__).resolve().parent.parent


def test_cmake_requires_and_records_clang_20() -> None:
    source = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20" in source
    assert "CMAKE_CXX_COMPILER_VERSION VERSION_LESS 21" in source
    assert "toolchain_provenance.txt" in source
    assert "_toolchain_provenance.txt" in source


def test_manylinux_uses_exact_clang_major_and_verified_runtime_sources() -> None:
    source = (ROOT / "pyproject.toml").read_text(encoding="utf-8")
    config = tomllib.loads(source)["tool"]["cibuildwheel"]["linux"]
    assert 'CC = "/usr/bin/clang-20"' in source
    assert 'CXX = "/usr/bin/clang++-20"' in source
    assert "dnf install -y clang20 " in source
    assert "clang_nvr=%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}" in source
    assert any(
        "rpm -q --qf '%{VERSION}' clang20 | grep -E '^20\\.'" in command for command in config["before-all"]
    )
    assert any("clang version 20\\." in command for command in config["before-all"])
    assert "llvm-project-20.1.8.src.tar.xz" in source
    assert "6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d" in source
    assert "clang version (1[89]|[2-9][0-9])" not in source


def test_conan_and_pep517_backend_are_exactly_constrained() -> None:
    pyproject = (ROOT / "pyproject.toml").read_text(encoding="utf-8")
    workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
    recipe = (ROOT / "conanfile.py").read_text(encoding="utf-8")

    assert '"scikit-build-core-conan==0.9.2"' in pyproject
    assert '"conan==2.30.0"' in pyproject
    assert '"pybind11==3.0.1"' in pyproject
    assert workflow.count("conan==2.30.0") == 3
    assert 'str(self.settings.compiler.version) != "20"' in recipe


def test_wheel_validation_requires_compiler_provenance() -> None:
    workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
    smoke = (ROOT / "tools" / "wheel_smoke_test.py").read_text(encoding="utf-8")
    template = (ROOT / "cmake" / "toolchain_provenance.txt.in").read_text(encoding="utf-8")

    assert "compiler_version=20." in workflow
    assert "compiler_version=20." in smoke
    assert "@CMAKE_CXX_COMPILER_VERSION@" in template

    repair = (ROOT / "tools" / "repair_wheel.py").read_text(encoding="utf-8")
    assert 'os.environ.get("CC", "clang")' in repair
    assert 'startswith("clang version 20.")' in repair


def main() -> int:
    tests = [value for name, value in globals().items() if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
        print(f"  PASS: {test.__name__}")
    print(f"\n{len(tests)}/{len(tests)} tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

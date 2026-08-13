"""Regression tests for Linux runtime ownership and the package contract.

These tests verify the source-level contracts that replace the old import-time
mutation of site-packages:

- ``__init__.py`` must NOT mutate the installed package directory at import time
  (no ``os.symlink``, ``os.remove``, or similar filesystem writes).
- ``__init__.py`` must fail closed with a clear ``ImportError`` if the native
  extension cannot be loaded.
- ``pyproject.toml`` must declare the Endstone runtime dependency.
- ``tools/repair_wheel.py`` must establish deterministic runtime ownership at
  build time via standard-SONAME bridge DSOs and fail closed on mismatch.
"""

from __future__ import annotations

import pathlib

import endstone_papi

_REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
_INIT_PY = _REPO_ROOT / "endstone_papi" / "__init__.py"
_PYPROJECT = _REPO_ROOT / "pyproject.toml"
_REPAIR_SCRIPT = _REPO_ROOT / "tools" / "repair_wheel.py"


def test_init_py_does_not_call_os_symlink_or_remove() -> None:
    """__init__.py must not create or delete files at import time."""
    source = _INIT_PY.read_text(encoding="utf-8")
    # These were the mutation primitives used by the old import-time bridge.
    assert "os.symlink" not in source, "import-time symlink creation must be removed"
    assert "os.remove" not in source, "import-time file deletion must be removed"
    assert "os.listdir" not in source, "import-time directory scanning must be removed"


def test_init_py_does_not_import_os_or_sys() -> None:
    """The mutation bridge required os/sys; the build-time approach does not."""
    source = _INIT_PY.read_text(encoding="utf-8")
    for line in source.splitlines():
        stripped = line.strip()
        if stripped.startswith("#"):
            continue
        assert not stripped.startswith("import os"), "os import should not be needed"
        assert not stripped.startswith("import sys"), "sys import should not be needed"


def test_init_py_has_fail_closed_import_error_handler() -> None:
    """If _papi cannot be loaded, __init__.py must raise a clear ImportError."""
    source = _INIT_PY.read_text(encoding="utf-8")
    assert "except ImportError" in source, "missing ImportError handler around _papi import"
    # The handler must chain the original exception and mention Endstone.
    assert "from _e" in source, "original exception must be chained"
    assert "endstone" in source.lower(), "error message should mention Endstone"


def test_no_libc_symlinks_in_package_directory() -> None:
    """No libc++ symlinks should exist in the package directory.

    The old import-time bridge created ``libc++.so.1`` / ``libc++abi.so.1``
    symlinks here.  The build-time NEEDED-patching approach does not.
    """
    pkg_dir = pathlib.Path(endstone_papi.__file__).resolve().parent
    for soname in ("libc++.so.1", "libc++abi.so.1"):
        assert not (pkg_dir / soname).exists(), f"{soname} must not exist in package directory"


def test_pyproject_declares_endstone_runtime_dependency() -> None:
    """pyproject.toml must declare endstone as a runtime dependency."""
    content = _PYPROJECT.read_text(encoding="utf-8")
    assert 'dependencies = ["endstone' in content, "endstone dependency not declared in [project].dependencies"


def test_pyproject_linux_before_build_installs_endstone() -> None:
    """The Linux cibuildwheel before-build must install endstone for repair_wheel.py."""
    content = _PYPROJECT.read_text(encoding="utf-8")
    assert "endstone==0.11.8" in content, "before-build must pin endstone for build-time SONAME discovery"
    assert "pip install ninja wheel endstone" in content, "before-build must install wheel for unpacking and repacking"


def test_pyproject_repair_wheel_uses_custom_script() -> None:
    """repair-wheel-command must use tools/repair_wheel.py, not bare auditwheel."""
    content = _PYPROJECT.read_text(encoding="utf-8")
    assert "tools/repair_wheel.py" in content, "repair-wheel-command must use the custom script"


def test_repair_wheel_script_exists() -> None:
    assert _REPAIR_SCRIPT.is_file(), "tools/repair_wheel.py must exist"


def test_repair_wheel_script_builds_standard_soname_bridges() -> None:
    """The script must build bridges, not mutate imports or rewrite _papi NEEDED."""
    source = _REPAIR_SCRIPT.read_text(encoding="utf-8")
    assert '"--add-needed"' in source, "bridge must depend on Endstone's hashed runtime"
    assert 'f"-Wl,-soname,{soname}"' in source, "bridge must expose the standard SONAME"
    assert "--replace-needed" not in source, "direct hashed-SONAME rewriting crashes module initialization"
    assert "os.symlink" not in source, "must not create symlinks"


def test_repair_wheel_script_uses_auditwheel_exclude() -> None:
    """The script must exclude libc++ from auditwheel to avoid a duplicate."""
    source = _REPAIR_SCRIPT.read_text(encoding="utf-8")
    assert '"--exclude"' in source or "'--exclude'" in source, "must use auditwheel --exclude"
    assert "libc++.so.1" in source
    assert "libc++abi.so.1" in source


def test_repair_wheel_script_fails_closed_on_missing_endstone() -> None:
    """If endstone is not installed, the script must exit non-zero."""
    source = _REPAIR_SCRIPT.read_text(encoding="utf-8")
    assert "cannot determine libc++ SONAMEs" in source, "must fail with clear message if endstone missing"


def test_repair_wheel_script_fails_closed_on_no_match() -> None:
    """If no hashed lib is found, the script must exit non-zero."""
    source = _REPAIR_SCRIPT.read_text(encoding="utf-8")
    assert "no hashed lib found" in source, "must fail with clear message if no match"


def test_repair_wheel_script_fails_closed_on_multiple_matches() -> None:
    """If multiple hashed libs match, the script must exit non-zero (ambiguous)."""
    source = _REPAIR_SCRIPT.read_text(encoding="utf-8")
    assert "multiple hashed libs" in source, "must fail with clear message if ambiguous match"


def test_repair_wheel_script_fails_closed_on_missing_patchelf() -> None:
    """If patchelf is not available, the script must exit non-zero."""
    source = _REPAIR_SCRIPT.read_text(encoding="utf-8")
    assert "patchelf not found" in source, "must fail with clear message if patchelf missing"


def test_repair_wheel_script_fails_closed_on_missing_compiler() -> None:
    """If the configured compiler is unavailable or not Clang 20, fail closed."""
    source = _REPAIR_SCRIPT.read_text(encoding="utf-8")
    assert "compiler {compiler_name!r} not found" in source
    assert "Clang 20 is required" in source
